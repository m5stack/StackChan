"""Console entry point for stackchan-mcp.

This module exists so that `import stackchan_mcp` (or any of its
submodules) does not trigger import-time side effects like
`load_dotenv()` or logging configuration. All such side effects live
inside :func:`main`, which is registered as the `stackchan-mcp`
console script in ``pyproject.toml`` and is also re-exported through
``stackchan_mcp.__main__`` so that ``python -m stackchan_mcp`` keeps
working.
"""

from __future__ import annotations

import atexit
import argparse
import asyncio
import errno
import logging
import os
import platform
import shutil
import socket
import subprocess
import sys
from typing import TYPE_CHECKING
from urllib.parse import parse_qsl, urlencode, urlparse, urlunparse

from . import __version__

if TYPE_CHECKING:
    from .ownership import LockInfo, LockMode

logger = logging.getLogger(__name__)


_DESCRIPTION = (
    "stdio MCP gateway for the StackChan / xiaozhi-esp32 firmware. "
    "Bridges stdio MCP clients (for example Claude Code) to a StackChan "
    "ESP32 device over WebSocket, and exposes an HTTP capture endpoint "
    "for photo uploads from the device."
)

_EPILOG = """\
Environment variables:
  STACKCHAN_TOKEN          Bearer token shared with the ESP32 firmware.
  VISION_URL               Full public capture URL (e.g. Tailscale Funnel).
  VISION_HOST              LAN IP of this machine, as seen from the ESP32.
  VISION_TOKEN             Optional separate token for VISION_URL uploads.
  STACKCHAN_AUDIO_HOOK_URL Enables device-driven listen capture push.
                           When set, Opus audio from a wake-word /
                           button / LCD-touch initiated listen window
                           is packed into Ogg/Opus and POSTed here.
                           Leave unset to keep the gateway's behaviour
                           unchanged from MCP-driven listen() only.
  STACKCHAN_AUDIO_HOOK_TOKEN
                           Bearer token for the audio hook endpoint;
                           falls back to STACKCHAN_TOKEN.
  STACKCHAN_LISTEN_LANGUAGE
                           Default listen() language when the call
                           omits language (default ja).
  HOST                     Bind address for the ESP32 WebSocket server
                           (default 0.0.0.0).
  WS_PORT                  Port for the ESP32 WebSocket server
                           (default 8765).
  CAPTURE_PORT             Port for the HTTP capture server
                           (default 8766).
  MCP_HTTP_HOST            Bind address for the Streamable HTTP MCP server
                           (default 127.0.0.1).
  MCP_HTTP_PORT            Port for the Streamable HTTP MCP server
                           (default 8767).
  MCP_HTTP_ALLOWED_HOSTS   Comma-separated Host / Origin allowlist entries
                           for non-loopback Streamable HTTP clients.

See gateway/README.md and the top-level README.md for full setup,
including pairing the ESP32 firmware and configuring the WiFi gateway URL.
"""

_STDIO_TRANSPORT = "stdio"
_STREAMABLE_HTTP_TRANSPORT = "streamable-http"
_TRANSPORT_CHOICES = (_STDIO_TRANSPORT, _STREAMABLE_HTTP_TRANSPORT)


def _build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="stackchan-mcp",
        description=_DESCRIPTION,
        epilog=_EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-V",
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Print the current gateway ownership lock status and exit.",
    )
    parser.add_argument(
        "--preflight",
        action="store_true",
        help=(
            "Run a non-destructive configuration and port preflight, then "
            "exit. Exit 0 if ready to run, non-zero if at least one "
            "blocking issue is found."
        ),
    )
    parser.add_argument(
        "--no-mdns",
        action="store_true",
        help="Disable mDNS/DNS-SD advertisement for the WebSocket endpoint.",
    )
    subparsers = parser.add_subparsers(dest="command", metavar="{serve}")
    serve_parser = subparsers.add_parser(
        "serve",
        help="Start the StackChan gateway.",
        description="Start the StackChan gateway using the selected transport.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    serve_parser.add_argument(
        "--transport",
        choices=_TRANSPORT_CHOICES,
        default=_STDIO_TRANSPORT,
        help="Gateway transport to serve (default: stdio).",
    )
    serve_parser.add_argument(
        "--no-mdns",
        dest="serve_no_mdns",
        action="store_true",
        help="Disable mDNS/DNS-SD advertisement for the WebSocket endpoint.",
    )
    return parser


# --- Preflight diagnostics (--check) ----------------------------------------
#
# The preflight is intentionally side-effect-free: it loads ``.env``, reads
# environment variables, attempts non-blocking ``bind()`` calls to the two
# server ports, and prints a concise human-readable report. It does NOT
# reach out to any ESP32, does not start either server, and does not modify
# any files. Live device connectivity belongs in a future ``status``
# subcommand (Issue #54 "Out of scope" note).


_BIND_ERROR_PREFIX = "bind error: "


def _check_port(host: str, port: int) -> tuple[bool, str | None]:
    """Probe ``(host, port)`` by trying to ``bind()`` it across every family.

    Resolves ``host`` via ``getaddrinfo`` with ``AF_UNSPEC`` and walks
    each (family, sockaddr) candidate so the preflight matches the
    same dual-stack behaviour as ``websockets.serve`` / ``aiohttp``.
    A literal ``::1`` or an IPv6-resolving ``localhost`` is therefore
    probed against the right address family rather than being
    misreported by an ``AF_INET``-only socket.

    Returns ``(available, info)``:

    - ``(True, None)``: at least one address family bound successfully.
      (Some IPv6 stacks fail with ``EADDRNOTAVAIL`` on hosts without a
      configured v6 interface; the gateway also tolerates that, so we
      report "ready" if any candidate succeeded.)
    - ``(False, "pid <N>, <cmd>")``: at least one candidate reported
      ``EADDRINUSE``. We short-circuit on the first one because the
      gateway will collide with the holder regardless of any other
      family that may have been free.
    - ``(False, None)``: same as above, but ``lsof`` could not identify
      the holder (or is unavailable on this platform).
    - ``(False, "bind error: <reason>")``: every candidate failed for
      a non-``EADDRINUSE`` reason (typically ``EADDRNOTAVAIL`` for an
      IP that is not assigned to any local interface, or ``EACCES`` on
      a privileged port without permission). Distinguishing this from
      "in use" prevents users from chasing a phantom process when the
      real issue is the bind address.
    """
    try:
        infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    except socket.gaierror as exc:
        return (False, f"{_BIND_ERROR_PREFIX}getaddrinfo failed: {exc}")

    last_error: str | None = None
    bound_at_least_once = False
    for family, socktype, proto, _canonname, sockaddr in infos:
        sock = socket.socket(family, socktype, proto)
        # Mirror ``asyncio.create_server``'s default behaviour on POSIX:
        # the gateway sets SO_REUSEADDR=1, so a port in TIME_WAIT after
        # a recent gateway restart would NOT actually block a fresh
        # bind. Without this option the preflight would misreport such
        # a port as IN USE and exit non-zero, even though the gateway
        # itself would start cleanly. SO_REUSEADDR does not let the
        # bind succeed when another process is currently LISTENing on
        # the port (POSIX semantics), so the EADDRINUSE branch below
        # still fires for genuine collisions.
        if hasattr(socket, "SO_REUSEADDR"):
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            except OSError:
                # Some platforms reject SO_REUSEADDR for certain socket
                # types; fall through and try the bind anyway.
                pass
        try:
            try:
                sock.bind(sockaddr)
            except OSError as exc:
                if exc.errno == errno.EADDRINUSE:
                    # Mirror gateway behaviour: an EADDRINUSE on any
                    # candidate family means the gateway will collide.
                    return (False, _try_get_port_holder(port))
                reason = exc.strerror or (
                    os.strerror(exc.errno)
                    if exc.errno is not None
                    else str(exc)
                )
                last_error = f"{_BIND_ERROR_PREFIX}{reason}"
            else:
                bound_at_least_once = True
        finally:
            sock.close()

    if bound_at_least_once:
        return (True, None)
    return (False, last_error)


def _try_get_port_holder(port: int) -> str | None:
    """Best-effort lookup of the process holding ``port`` via ``lsof``.

    Returns ``"pid <N>, <cmd>"`` on success, or ``None`` if ``lsof`` is
    not installed, the call fails, or the port is not in fact held (for
    example, the bind failure was due to a permission error rather than
    EADDRINUSE).
    """
    if shutil.which("lsof") is None:
        return None
    try:
        result = subprocess.run(
            ["lsof", f"-iTCP:{port}", "-sTCP:LISTEN", "-Fpcn"],
            capture_output=True,
            text=True,
            timeout=2,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0 or not result.stdout:
        return None
    pid: str | None = None
    cmd: str | None = None
    for line in result.stdout.splitlines():
        if line.startswith("p"):
            pid = line[1:]
        elif line.startswith("c"):
            cmd = line[1:]
    if pid and cmd:
        return f"pid {pid}, {cmd}"
    if pid:
        return f"pid {pid}"
    return None


def _format_port_status(available: bool, holder: str | None) -> str:
    if available:
        return "AVAILABLE"
    if holder is None:
        return "IN USE"
    if holder.startswith(_BIND_ERROR_PREFIX):
        # Don't say "IN USE" for non-EADDRINUSE bind failures
        # (EADDRNOTAVAIL, EACCES, etc.). Surface the actual reason
        # instead so the user does not chase a phantom process.
        reason = holder.removeprefix(_BIND_ERROR_PREFIX)
        return f"BIND ERROR ({reason})"
    return f"IN USE ({holder})"


_TCP_PORT_RANGE = range(0, 65536)


def _validate_port_value(raw: str, var: str) -> tuple[int | None, str]:
    """Parse ``raw`` as a TCP port, returning ``(port, source_or_error)``.

    Returns ``(int_value, var)`` for a valid in-range integer (0..65535
    inclusive — ``0`` lets the OS pick, which the gateway may not
    actually want but is at least bind-able). Returns ``(None, "<var>=
    <raw> (...)")`` otherwise; the caller treats that as a blocking
    issue rather than silently falling through to a default.

    Both branches matter for the preflight: ``socket.bind()`` raises
    ``OverflowError`` for values outside the TCP port range, so without
    this validation ``--check`` would crash with a stack trace instead
    of producing the diagnostic report it exists to produce.
    """
    try:
        value = int(raw)
    except ValueError:
        return (None, f"{var}={raw!r} (not an integer)")
    if value not in _TCP_PORT_RANGE:
        return (None, f"{var}={raw!r} (out of TCP port range 0-65535)")
    return (value, var)


def _resolve_ws_port() -> tuple[int | None, str]:
    """Resolve the WebSocket port using the same precedence as ``gateway.py``.

    Mirrors ``int(os.getenv("WS_PORT", os.getenv("PORT", "8765")))`` from
    ``gateway.py`` so the preflight checks the port the gateway will
    actually bind, not a hard-coded default. See ``_validate_port_value``
    for the validation rules; on success returns ``(port, "WS_PORT")``
    or ``(port, "PORT")``, otherwise ``(None, "<var>=<raw> (...)")``.
    """
    for var in ("WS_PORT", "PORT"):
        raw = os.getenv(var)
        if raw is None:
            continue
        return _validate_port_value(raw, var)
    return (8765, "default")


def _resolve_capture_port() -> tuple[int | None, str]:
    """Resolve the HTTP capture port using ``gateway.py``'s precedence.

    Mirrors ``int(os.getenv("CAPTURE_PORT", "8766"))``. See
    ``_validate_port_value`` for the validation rules.
    """
    raw = os.getenv("CAPTURE_PORT")
    if raw is None:
        return (8766, "default")
    return _validate_port_value(raw, "CAPTURE_PORT")


# Exact query parameter names that are always redacted in preflight
# output. Compared lowercased.
_SECRET_QUERY_KEYS = frozenset(
    {
        "access_token",
        "api_key",
        "apikey",
        "auth",
        "auth_token",
        "key",
        "password",
        "secret",
        "sig",
        "signature",
        "token",
    }
)

# Suffix-based heuristic for redacting provider-specific signed-URL
# parameters without enumerating every variant. Matches things like
# ``X-Amz-Signature``, ``X-Amz-Security-Token``, ``X-Goog-Signature``,
# Azure SAS ``sig`` (already covered by the exact set), generic
# ``*_token`` / ``*-secret`` patterns, etc. Compared lowercased.
_SECRET_QUERY_KEY_SUFFIXES = (
    "signature",
    "token",
    "secret",
    "password",
    "credential",
    "credentials",
)


def _is_secret_query_key(key: str) -> bool:
    lower = key.lower()
    if lower in _SECRET_QUERY_KEYS:
        return True
    return any(lower.endswith(suffix) for suffix in _SECRET_QUERY_KEY_SUFFIXES)


def _redact_url_secrets(url: str) -> str:
    """Mask userinfo and secret-looking query params in ``url``.

    The preflight output is meant to be safe to paste into a public
    issue or log, so any in-URL credential is replaced before
    printing:

    - ``https://user:pass@host/path`` → ``https://***:***@host/path``
    - ``?token=abc&page=1`` → ``?token=%2A%2A%2Aredacted%2A%2A%2A&page=1``
      (only keys in ``_SECRET_QUERY_KEYS`` are touched; non-secret
      params keep their value)

    Non-credential structure (scheme, host, port, path, fragment,
    non-secret query keys) is preserved so users can still see what
    the gateway is actually configured to call. Inputs that fail to
    parse are returned unchanged so the preflight never crashes on a
    malformed value.
    """
    try:
        parsed = urlparse(url)
    except (ValueError, TypeError):
        return url

    netloc = parsed.netloc
    if "@" in netloc:
        # Strip userinfo and replace with a fixed placeholder. Don't
        # try to preserve the username — the username alone can leak
        # information and the structural info we care about (host,
        # port) lives after the @.
        _userinfo, _, host_part = netloc.rpartition("@")
        netloc = f"***:***@{host_part}"

    query = parsed.query
    if query:
        try:
            params = parse_qsl(query, keep_blank_values=True)
        except ValueError:
            params = None
        if params is not None:
            redacted = [
                (k, "***redacted***") if _is_secret_query_key(k) else (k, v)
                for k, v in params
            ]
            query = urlencode(redacted)

    return urlunparse(
        (parsed.scheme, netloc, parsed.path, parsed.params, query, parsed.fragment)
    )


def _load_dotenv() -> None:
    """Lazy ``.env`` loader exposed as a single attachable seam.

    Wrapping ``python-dotenv`` here keeps two properties:

    1. ``import stackchan_mcp.cli`` stays side-effect free (the
       ``dotenv`` import only happens when the gateway / preflight is
       actually invoked).
    2. Tests can ``monkeypatch.setattr(cli, "_load_dotenv", ...)`` to
       prevent the real ``find_dotenv()`` walking up to the developer's
       ``gateway/.env`` and contaminating environment-isolation tests.
    """
    from dotenv import load_dotenv

    load_dotenv()


def _run_ownership_check() -> int:
    """Print the current ownership lock status and exit cleanly."""
    from .ownership import is_pid_alive, read_lock

    # Load ``.env`` first so that a WS_PORT / PORT defined only there resolves
    # the same per-port lock the running gateway claims. Startup loads ``.env``
    # before ``_acquire_startup_lock`` (via ``_run_stdio_gateway`` /
    # ``_run_streamable_http_placeholder``), so ``--check`` must match it —
    # otherwise a gateway owning ``owner-18765.lock`` would be inspected against
    # the default ``owner-8765.lock`` and wrongly reported ready. Mirrors how
    # ``--preflight`` loads ``.env`` inside ``_run_preflight``.
    _load_dotenv()
    # Inspect the per-WS_PORT lock this gateway would claim (not the legacy
    # machine-global owner.lock), so the preflight reflects the actual port.
    lock_path = _ws_port_lock_path()
    info = read_lock(lock_path)
    if info is None:
        print(f"no current owner (lock: {lock_path.name})")
        print("ownership preflight: ready")
        print("Result: ready. Exit 0.")
    elif is_pid_alive(info["pid"]):
        fields = [
            f"owner_id={info['owner_id']}",
            f"pid={info['pid']}",
            f"start_ts={info['start_ts']}",
            f"host={info['host']}",
        ]
        for key in ("mode", "http_endpoint", "started_by"):
            if key in info:
                fields.append(f"{key}={info[key]}")
        print(" ".join(fields))
    else:
        print(f"stale lock found: pid {info['pid']} not alive")
    return 0


# Default Homebrew prefixes that ship libopus.dylib on macOS. Apple
# Silicon installs default to ``/opt/homebrew``; Intel Macs use
# ``/usr/local``. Keeping both keeps the helper portable across
# contributor machines.
_HOMEBREW_LIB_DIRS = ("/opt/homebrew/lib", "/usr/local/lib")


def _ensure_libopus_findable() -> None:
    """Make libopus reachable to opuslib's ``ctypes.find_library`` on macOS.

    ``opuslib.api`` calls ``ctypes.util.find_library("opus")`` at
    import time. On macOS that walks ``DYLD_LIBRARY_PATH`` plus a
    couple of system-default directories — but not Homebrew's
    ``/opt/homebrew/lib`` (Apple Silicon) or ``/usr/local/lib`` (Intel),
    so a vanilla ``brew install opus`` lands a working libopus that
    opuslib still cannot find. Users then see ``Could not find Opus
    library`` even though the dylib is on disk.

    Prepend any Homebrew-style lib directories that exist so the next
    ``find_library`` call (triggered by the lazy ``import opuslib``
    inside :func:`audio_utils.encode_opus_frames`) succeeds. We
    deliberately *prepend* and skip duplicates so an explicit
    ``DYLD_LIBRARY_PATH`` set by the operator (e.g. for a custom build
    of libopus) keeps priority. No-op on non-macOS hosts.
    """
    if platform.system() != "Darwin":
        return

    existing = os.environ.get("DYLD_LIBRARY_PATH", "")
    paths: list[str] = [p for p in existing.split(":") if p]

    prepended: list[str] = []
    for candidate in _HOMEBREW_LIB_DIRS:
        if candidate in paths:
            continue
        if not os.path.isdir(candidate):
            continue
        prepended.append(candidate)

    if not prepended:
        return

    os.environ["DYLD_LIBRARY_PATH"] = ":".join(prepended + paths)
    logger.debug(
        "Prepended Homebrew lib dirs to DYLD_LIBRARY_PATH so opuslib "
        "can find libopus: %s",
        prepended,
    )


def _run_preflight() -> int:
    """Run preflight diagnostics. Returns the desired process exit code.

    Output is intentionally fixed-width and grep-friendly. Exit 0 means
    "ready to run"; non-zero means at least one blocking issue (currently
    only port unavailability). Missing optional configuration is reported
    but does not fail the check, mirroring how the gateway itself only
    warns about a missing ``STACKCHAN_TOKEN``.
    """
    _load_dotenv()
    _ensure_libopus_findable()

    issues = 0
    print(f"stackchan-mcp {__version__} preflight")
    print()

    # --- Configuration ------------------------------------------------------
    print("Configuration:")
    token = os.getenv("STACKCHAN_TOKEN") or os.getenv("BEARER_TOKEN")
    if token:
        print("  STACKCHAN_TOKEN     set (***redacted***)")
    else:
        print("  STACKCHAN_TOKEN     not set (gateway will accept any client)")

    mcp_http_allowed_hosts = os.getenv("MCP_HTTP_ALLOWED_HOSTS", "")
    if mcp_http_allowed_hosts:
        print(f"  MCP_HTTP_ALLOWED_HOSTS {mcp_http_allowed_hosts}")
    else:
        print("  MCP_HTTP_ALLOWED_HOSTS not set")

    vision_host = os.getenv("VISION_HOST", "")
    capture_port_raw = os.getenv("CAPTURE_PORT", "8766")
    if vision_host:
        print(f"  VISION_HOST         {vision_host}")
    else:
        print("  VISION_HOST         not set")

    vision_url_explicit = os.getenv("VISION_URL", "")
    if vision_url_explicit:
        print(f"  VISION_URL          {_redact_url_secrets(vision_url_explicit)}")
    elif vision_host:
        # Derived URL has no userinfo or query params, so no redaction
        # needed; the host part is shown as-is by design (it is the IP
        # the user has configured for capture).
        derived = f"http://{vision_host}:{capture_port_raw}/capture"
        print(f"  VISION_URL          (derived) {derived}")
    else:
        print(
            "  VISION_URL          not set "
            "(set VISION_HOST or VISION_URL for take_photo)"
        )

    if os.getenv("VISION_TOKEN"):
        print("  VISION_TOKEN        set (***redacted***)")
    else:
        print("  VISION_TOKEN        not set (will reuse STACKCHAN_TOKEN)")

    audio_hook_url = os.getenv("STACKCHAN_AUDIO_HOOK_URL", "")
    if audio_hook_url:
        print(
            f"  STACKCHAN_AUDIO_HOOK_URL  {_redact_url_secrets(audio_hook_url)}"
        )
        if os.getenv("STACKCHAN_AUDIO_HOOK_TOKEN"):
            print("  STACKCHAN_AUDIO_HOOK_TOKEN set (***redacted***)")
        else:
            print(
                "  STACKCHAN_AUDIO_HOOK_TOKEN not set "
                "(will reuse STACKCHAN_TOKEN)"
            )
    else:
        print(
            "  STACKCHAN_AUDIO_HOOK_URL  not set "
            "(device-driven listen capture disabled)"
        )

    # --- Ports --------------------------------------------------------------
    print()
    print("Ports:")
    host = os.getenv("HOST", "0.0.0.0")
    mcp_http_host = os.getenv("MCP_HTTP_HOST", "127.0.0.1")
    ws_port, ws_source = _resolve_ws_port()
    cap_port, cap_source = _resolve_capture_port()
    raw_mcp_http_port = os.getenv("MCP_HTTP_PORT")
    if raw_mcp_http_port is None:
        mcp_http_port, mcp_http_source = (8767, "default")
    else:
        mcp_http_port, mcp_http_source = _validate_port_value(
            raw_mcp_http_port,
            "MCP_HTTP_PORT",
        )

    if ws_port is None:
        print(f"  ws://{host}:???     INVALID ({ws_source})")
        issues += 1
    if cap_port is None:
        print(f"  http://{host}:???   INVALID ({cap_source})")
        issues += 1
    if mcp_http_port is None:
        print(f"  http://{mcp_http_host}:???/mcp INVALID ({mcp_http_source})")
        issues += 1

    if (
        ws_port is not None
        and cap_port is not None
        and ws_port == cap_port
        and ws_port != 0
    ):
        # The gateway runs WebSocket and HTTP capture as separate
        # listeners; binding the WebSocket server first will then make
        # the HTTP bind fail. Independent _check_port probes can't see
        # this on their own (each one binds-and-releases), so we surface
        # the conflict explicitly.
        #
        # Port 0 is excluded: each ``bind((host, 0))`` asks the OS for a
        # fresh ephemeral port, so two listeners both configured with 0
        # do NOT collide (this is exactly the configuration the existing
        # gateway tests use).
        print(
            f"  WS_PORT ({ws_source}) and CAPTURE_PORT ({cap_source}) "
            f"both resolve to {ws_port}; the gateway needs distinct ports."
        )
        issues += 1

    if mcp_http_port is not None:
        for label, other_port, other_source in (
            ("WS_PORT", ws_port, ws_source),
            ("CAPTURE_PORT", cap_port, cap_source),
        ):
            if other_port is None or mcp_http_port == 0 or other_port == 0:
                continue
            if mcp_http_port == other_port:
                print(
                    f"  MCP_HTTP_PORT ({mcp_http_source}) and {label} "
                    f"({other_source}) both resolve to {mcp_http_port}; "
                    "the daemon needs distinct listener ports."
                )
                issues += 1

    if ws_port is not None:
        ws_available, ws_holder = _check_port(host, ws_port)
        print(
            f"  ws://{host}:{ws_port}   "
            f"{_format_port_status(ws_available, ws_holder)}"
        )
        if not ws_available:
            issues += 1

    if cap_port is not None:
        cap_available, cap_holder = _check_port(host, cap_port)
        print(
            f"  http://{host}:{cap_port} "
            f"{_format_port_status(cap_available, cap_holder)}"
        )
        if not cap_available:
            issues += 1

    if mcp_http_port is not None:
        from .http_server import validate_bind_safety

        mcp_available, mcp_holder = _check_port(mcp_http_host, mcp_http_port)
        print(
            f"  http://{mcp_http_host}:{mcp_http_port}/mcp "
            f"{_format_port_status(mcp_available, mcp_holder)}"
        )
        if not mcp_available:
            issues += 1
        try:
            validate_bind_safety(mcp_http_host, token)
        except ValueError as exc:
            print(f"  MCP HTTP bind safety: BLOCKED ({exc})")
            issues += 1

    # --- Result -------------------------------------------------------------
    print()
    if issues == 0:
        print("Result: ready. Exit 0.")
        return 0
    plural = "s" if issues > 1 else ""
    print(f"Result: {issues} issue{plural}. Exit 1.")
    return 1


async def _run(*, advertise_mdns: bool = True) -> None:
    """Start both the ESP32 WebSocket server and the stdio MCP server."""
    import signal

    from .event_log import rotate_old_entries
    from .gateway import get_gateway
    from .notify_config import load_notify_config
    from .stdio_server import run_stdio_server

    notify_config = load_notify_config()
    gateway = get_gateway()
    esp32 = getattr(gateway, "esp32", None)
    set_notify_config = getattr(esp32, "set_notify_config", None)
    if callable(set_notify_config):
        set_notify_config(notify_config)

    loop = asyncio.get_running_loop()
    main_task = asyncio.current_task()

    def _handle_sigterm() -> None:
        if main_task and not main_task.done():
            main_task.cancel()

    if sys.platform != "win32":
        loop.add_signal_handler(signal.SIGTERM, _handle_sigterm)

    # Prune stale stackchan-event log entries only when the JSONL path is
    # explicitly enabled. With the default all-OFF notify config, gateway
    # startup must not create or rewrite any persistent event-log files.
    if notify_config.jsonl_enabled:
        rotate_old_entries(path=notify_config.jsonl_path)

    await gateway.start(advertise_mdns=advertise_mdns)
    logger.info("Gateway started, waiting for ESP32 connections...")

    try:
        # Run stdio MCP server (blocks until MCP client disconnects)
        await run_stdio_server(notify_config=notify_config)
    except asyncio.CancelledError:
        logger.info("Received termination signal, shutting down...")
    finally:
        await gateway.stop()


def _configure_gateway_startup() -> None:
    """Load runtime configuration and logging for gateway startup paths."""
    _load_dotenv()
    _ensure_libopus_findable()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    from .user_defaults import log_user_defaults_startup

    log_user_defaults_startup()


def _ws_port_lock_path():
    """Per-WS_PORT ownership lock path.

    複数機体対応: ownership lock を machine-global な ``owner.lock`` でなく
    WS_PORT 単位に scope する。 lock は「同一 device を 2 つの gateway が
    奪い合わない」 ための機構だが、 1 device = 1 gateway = 1 WS ポートで
    複数機体を同時に動かす構成では global lock が 2 台目以降を誤って弾く。
    ポート単位なら N gateway が共存でき、 同一ポート二重起動は従来通り弾ける。
    WS_PORT 未解決時は従来の global パスにフォールバック。
    """
    from .ownership import LOCK_DIR

    ws_port, _ = _resolve_ws_port()
    return LOCK_DIR / (
        f"owner-{ws_port}.lock" if ws_port is not None else "owner.lock"
    )


def _acquire_startup_lock(
    *,
    mode: "LockMode" = _STDIO_TRANSPORT,
    http_endpoint: str | None = None,
    started_by: str | None = None,
) -> "LockInfo":
    """Claim the gateway ownership lock and register normal cleanup."""
    from .ownership import (
        OwnershipError,
        acquire_lock,
        generate_owner_id,
        release_lock_if_owner,
    )

    lock_path = _ws_port_lock_path()

    owner_id = generate_owner_id()
    try:
        if mode == _STDIO_TRANSPORT and http_endpoint is None and started_by is None:
            info = acquire_lock(owner_id, path=lock_path)
        else:
            info = acquire_lock(
                owner_id,
                path=lock_path,
                mode=mode,
                http_endpoint=http_endpoint,
                started_by=started_by,
            )
    except OwnershipError as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)

    try:
        print(
            "stackchan-mcp: acquired ownership lock "
            f"(owner_id={info['owner_id']}, pid={info['pid']}, "
            f"lock={lock_path.name})",
            file=sys.stderr,
        )
        atexit.register(release_lock_if_owner, info, lock_path)
    except BaseException:
        release_lock_if_owner(info, lock_path)
        raise

    return info


def _prepare_stdio_startup() -> "LockInfo":
    """Prepare the existing stdio gateway flow without changing its lock shape."""
    _configure_gateway_startup()
    return _acquire_startup_lock()


def _run_stdio_gateway(*, advertise_mdns: bool = True) -> None:
    """Run the existing stdio MCP gateway flow."""
    from .ownership import release_lock_if_owner

    info = _prepare_stdio_startup()
    try:
        try:
            asyncio.run(_run(advertise_mdns=advertise_mdns))
        except KeyboardInterrupt:
            pass
    finally:
        release_lock_if_owner(info, _ws_port_lock_path())


def _resolve_mcp_http_endpoint() -> tuple[str, int]:
    """Resolve the Streamable HTTP daemon endpoint from environment."""
    host = os.getenv("MCP_HTTP_HOST", "127.0.0.1")
    raw_port = os.getenv("MCP_HTTP_PORT", "8767")
    port, source = _validate_port_value(raw_port, "MCP_HTTP_PORT")
    if port is None:
        print(f"stackchan-mcp: invalid MCP_HTTP_PORT: {source}", file=sys.stderr)
        sys.exit(1)
    return host, port


async def _run_streamable_http_daemon(
    *,
    host: str,
    port: int,
    owner_id: str,
    token: str | None,
    advertise_mdns: bool,
) -> None:
    """Run the Streamable HTTP MCP daemon until the ASGI server exits."""
    import uvicorn

    from .event_log import rotate_old_entries
    from .gateway import get_gateway
    from .notify_config import load_notify_config
    from .http_server import build_app, make_dispatch_fn
    from .queue import CommandQueue

    notify_config = load_notify_config()
    if notify_config.jsonl_enabled:
        rotate_old_entries(path=notify_config.jsonl_path)

    gateway = get_gateway()
    esp32 = getattr(gateway, "esp32", None)
    set_notify_config = getattr(esp32, "set_notify_config", None)
    if callable(set_notify_config):
        set_notify_config(notify_config)
    queue = CommandQueue()
    app = build_app(
        queue,
        gateway=gateway,
        owner_id=owner_id,
        host=host,
        port=port,
        token=token,
        dispatch_fn=make_dispatch_fn(gateway),
        notify_config=notify_config,
    )
    config = uvicorn.Config(
        app,
        host=host,
        port=port,
        log_level="info",
        lifespan="on",
    )
    server = uvicorn.Server(config)

    await gateway.start(advertise_mdns=advertise_mdns)
    logger.info(
        "Streamable HTTP MCP daemon starting on http://%s:%d/mcp",
        host,
        port,
    )
    try:
        await server.serve()
    finally:
        await gateway.stop()


def _run_streamable_http_placeholder(*, advertise_mdns: bool = True) -> None:
    """Run the Streamable HTTP MCP daemon."""
    from .ownership import release_lock_if_owner
    from .http_server import (
        get_configured_token,
        validate_bind_safety,
    )

    _configure_gateway_startup()
    host, port = _resolve_mcp_http_endpoint()
    token = get_configured_token()
    try:
        validate_bind_safety(host, token)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)

    info: LockInfo | None = None
    try:
        info = _acquire_startup_lock(
            mode=_STREAMABLE_HTTP_TRANSPORT,
            http_endpoint=f"{host}:{port}",
            started_by="cli-serve",
        )
        try:
            asyncio.run(
                _run_streamable_http_daemon(
                    host=host,
                    port=port,
                    owner_id=info["owner_id"],
                    token=token,
                    advertise_mdns=advertise_mdns,
                )
            )
        except KeyboardInterrupt:
            pass
    finally:
        if info is not None:
            release_lock_if_owner(info, _ws_port_lock_path())


def main(argv: list[str] | None = None) -> None:
    """Console-script entry point.

    Parses ``--help`` / ``--version`` / ``--check`` / ``--preflight`` early
    (without starting the server), then dispatches either the legacy
    zero-subcommand stdio flow or the ``serve`` subcommand. Side effects
    are intentionally scoped below argument parsing so that
    ``import stackchan_mcp`` stays clean.
    """
    parser = _build_arg_parser()
    # argparse exits with status 0 on --help / --version before reaching
    # any of the gateway start-up below, which is the intended behaviour.
    args = parser.parse_args(argv)

    if args.check:
        sys.exit(_run_ownership_check())

    if args.preflight:
        # ``_run_preflight`` loads ``.env`` itself; do not double-load
        # via the path below.
        sys.exit(_run_preflight())

    if args.command is None:
        _run_stdio_gateway(advertise_mdns=not args.no_mdns)
        return

    if args.command == "serve":
        advertise_mdns = not (args.no_mdns or getattr(args, "serve_no_mdns", False))
        if args.transport == _STDIO_TRANSPORT:
            _run_stdio_gateway(advertise_mdns=advertise_mdns)
            return
        _run_streamable_http_placeholder(advertise_mdns=advertise_mdns)
        return


if __name__ == "__main__":
    main()
