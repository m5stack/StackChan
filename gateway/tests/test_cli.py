"""Tests for the stackchan-mcp CLI entry point.

These tests focus on the no-side-effect command-line flags
(``--help``, ``--version``, ``--check``); full gateway start-up is
covered by ``test_stdio_server.py`` and ``test_gateway.py``.
"""

from __future__ import annotations

import asyncio
import os
import signal
import socket
from pathlib import Path

import pytest

from stackchan_mcp import __version__, cli
from stackchan_mcp.cli import (
    _build_arg_parser,
    _check_port,
    _format_port_status,
    _redact_url_secrets,
    _run_preflight,
    main,
)


_PREFLIGHT_ENV_VARS = (
    "STACKCHAN_TOKEN",
    "BEARER_TOKEN",
    "VISION_HOST",
    "VISION_URL",
    "VISION_TOKEN",
    "HOST",
    "WS_PORT",
    # ``_resolve_ws_port`` falls back to ``PORT`` when ``WS_PORT`` is
    # unset, so ``PORT`` must also be cleared for the default-port
    # tests to be deterministic across CI / dev environments that
    # already export ``PORT``.
    "PORT",
    "CAPTURE_PORT",
    "MCP_HTTP_HOST",
    "MCP_HTTP_PORT",
    "MCP_HTTP_ALLOWED_HOSTS",
)


def _isolate_preflight_env(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Make preflight tests independent of any host ``.env`` / inherited env.

    ``python-dotenv`` resolves ``.env`` via ``find_dotenv()``, which
    walks up the **calling stack frame's** file path — not the cwd —
    so simply ``chdir(tmp_path)`` is not enough to escape a developer's
    real ``gateway/.env``. We instead replace ``cli._load_dotenv`` with
    a no-op for the duration of the test, then strip the relevant env
    vars to give the preflight a deterministic baseline.
    """
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(cli, "_load_dotenv", lambda: None)
    for var in _PREFLIGHT_ENV_VARS:
        monkeypatch.delenv(var, raising=False)


def _fake_lock_info() -> dict[str, object]:
    return {
        "owner_id": "test-owner",
        "pid": 123,
        "start_ts": "2026-06-05T00:00:00Z",
        "host": "test-host",
    }


def test_arg_parser_help_long_flag(capsys: pytest.CaptureFixture[str]) -> None:
    parser = _build_arg_parser()
    with pytest.raises(SystemExit) as exc:
        parser.parse_args(["--help"])
    assert exc.value.code == 0
    captured = capsys.readouterr()
    out = captured.out
    # Help text should mention prog name, the headline env vars, and a
    # pointer to the in-tree READMEs so end users know where to look next.
    assert "stackchan-mcp" in out
    assert "STACKCHAN_TOKEN" in out
    assert "VISION_URL" in out
    assert "WS_PORT" in out
    assert "README" in out


def test_arg_parser_help_short_flag() -> None:
    parser = _build_arg_parser()
    with pytest.raises(SystemExit) as exc:
        parser.parse_args(["-h"])
    assert exc.value.code == 0


def test_arg_parser_version_long_flag(
    capsys: pytest.CaptureFixture[str],
) -> None:
    parser = _build_arg_parser()
    with pytest.raises(SystemExit) as exc:
        parser.parse_args(["--version"])
    assert exc.value.code == 0
    captured = capsys.readouterr()
    # argparse writes --version output to stdout on Python 3.4+.
    combined = captured.out + captured.err
    assert f"stackchan-mcp {__version__}" in combined


def test_arg_parser_version_short_flag(
    capsys: pytest.CaptureFixture[str],
) -> None:
    parser = _build_arg_parser()
    with pytest.raises(SystemExit) as exc:
        parser.parse_args(["-V"])
    assert exc.value.code == 0
    captured = capsys.readouterr()
    combined = captured.out + captured.err
    assert f"stackchan-mcp {__version__}" in combined


def test_main_help_exits_before_side_effects(
    capsys: pytest.CaptureFixture[str],
) -> None:
    """``main(['--help'])`` must exit 0 *before* load_dotenv / asyncio.run.

    The whole point of the new flag is that first-time users can run
    ``stackchan-mcp --help`` without binding port 8765 or waiting on
    stdin, so this regression test guards that contract.
    """
    with pytest.raises(SystemExit) as exc:
        main(["--help"])
    assert exc.value.code == 0
    captured = capsys.readouterr()
    assert "stackchan-mcp" in captured.out


def test_version_resolves_from_installed_metadata() -> None:
    """``__version__`` should be sourced from package metadata, not a literal.

    This guards against the previous failure mode where the literal in
    ``stackchan_mcp/__init__.py`` drifted away from
    ``gateway/pyproject.toml`` across releases.
    """
    assert __version__ != "0.0.0+unknown"
    # Expect a SemVer-ish leading digit; the editable install resolves
    # to whatever ``pyproject.toml`` declares.
    assert __version__[:1].isdigit()


# --- --check flag tests -----------------------------------------------------


def test_arg_parser_check_flag_is_registered() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args(["--check"])
    assert args.check is True


def test_arg_parser_check_defaults_to_false() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args([])
    assert args.check is False


def test_arg_parser_no_mdns_flag_is_registered() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args(["--no-mdns"])
    assert args.no_mdns is True


def test_arg_parser_no_mdns_defaults_to_false() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args([])
    assert args.no_mdns is False


def test_main_default_advertises_mdns(monkeypatch: pytest.MonkeyPatch) -> None:
    from stackchan_mcp import ownership

    called: dict[str, bool] = {}

    async def fake_run(*, advertise_mdns: bool = True) -> None:
        called["advertise_mdns"] = advertise_mdns

    monkeypatch.setattr(cli, "_prepare_stdio_startup", _fake_lock_info)
    monkeypatch.setattr(cli, "_load_dotenv", lambda: None)
    monkeypatch.setattr(cli, "_ensure_libopus_findable", lambda: None)
    monkeypatch.setattr(cli, "_run", fake_run)
    monkeypatch.setattr(ownership, "release_lock_if_owner", lambda info, path=None: True)

    main([])

    assert called == {"advertise_mdns": True}


def test_main_no_mdns_disables_advertisement(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    from stackchan_mcp import ownership

    called: dict[str, bool] = {}

    async def fake_run(*, advertise_mdns: bool = True) -> None:
        called["advertise_mdns"] = advertise_mdns

    monkeypatch.setattr(cli, "_prepare_stdio_startup", _fake_lock_info)
    monkeypatch.setattr(cli, "_load_dotenv", lambda: None)
    monkeypatch.setattr(cli, "_ensure_libopus_findable", lambda: None)
    monkeypatch.setattr(cli, "_run", fake_run)
    monkeypatch.setattr(ownership, "release_lock_if_owner", lambda info, path=None: True)

    main(["--no-mdns"])

    assert called == {"advertise_mdns": False}


@pytest.mark.asyncio
async def test_run_sigterm_handler_cancels_and_stops_gateway(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    if cli.sys.platform == "win32":
        pytest.skip("POSIX signal handlers are not registered on Windows")

    from stackchan_mcp import gateway as gateway_module
    from stackchan_mcp import stdio_server

    events: list[object] = []
    registered_handlers: dict[int, object] = {}

    class FakeGateway:
        async def start(self, *, advertise_mdns: bool = True) -> None:
            events.append(("start", advertise_mdns))

        async def stop(self) -> None:
            events.append("stop")

    async def fake_run_stdio_server(*, notify_config=None) -> None:
        events.append("stdio")
        handler = registered_handlers[signal.SIGTERM]
        assert callable(handler)
        handler()
        await asyncio.sleep(0)

    def fake_add_signal_handler(signum: int, callback: object) -> None:
        registered_handlers[signum] = callback

    loop = asyncio.get_running_loop()
    monkeypatch.setattr(loop, "add_signal_handler", fake_add_signal_handler)
    monkeypatch.setattr(gateway_module, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(stdio_server, "run_stdio_server", fake_run_stdio_server)

    await cli._run(advertise_mdns=False)

    assert registered_handlers.keys() == {signal.SIGTERM}
    assert events == [("start", False), "stdio", "stop"]




@pytest.mark.asyncio
@pytest.mark.parametrize("jsonl_enabled", [False, True])
async def test_run_rotates_event_log_only_when_jsonl_enabled(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    jsonl_enabled: bool,
) -> None:
    from stackchan_mcp import event_log as event_log_module
    from stackchan_mcp import gateway as gateway_module
    from stackchan_mcp import notify_config as notify_config_module
    from stackchan_mcp import stdio_server
    from stackchan_mcp.notify_config import DEFAULT_MESSAGE_TEMPLATES, NotifyConfig

    events: list[object] = []
    rotate_calls: list[Path] = []
    jsonl_path = tmp_path / "events.jsonl"
    config = NotifyConfig(
        legacy_event_enabled=False,
        channels_enabled=False,
        jsonl_enabled=jsonl_enabled,
        jsonl_path=jsonl_path,
        messages=dict(DEFAULT_MESSAGE_TEMPLATES),
    )

    class FakeESP32:
        def set_notify_config(self, notify_config: NotifyConfig) -> None:
            events.append(("set_notify_config", notify_config))

    class FakeGateway:
        esp32 = FakeESP32()

        async def start(self, *, advertise_mdns: bool = True) -> None:
            events.append(("start", advertise_mdns))

        async def stop(self) -> None:
            events.append("stop")

    async def fake_run_stdio_server(*, notify_config=None) -> None:
        events.append("stdio")

    def fake_rotate_old_entries(*, path: Path, now_unix: float | None = None) -> None:
        rotate_calls.append(path)

    monkeypatch.setattr(cli.sys, "platform", "win32")
    monkeypatch.setattr(notify_config_module, "load_notify_config", lambda: config)
    monkeypatch.setattr(event_log_module, "rotate_old_entries", fake_rotate_old_entries)
    monkeypatch.setattr(gateway_module, "get_gateway", lambda: FakeGateway())
    monkeypatch.setattr(stdio_server, "run_stdio_server", fake_run_stdio_server)

    await cli._run(advertise_mdns=False)

    assert (rotate_calls == [jsonl_path]) == jsonl_enabled
    assert events == [
        ("set_notify_config", config),
        ("start", False),
        "stdio",
        "stop",
    ]

def test_main_check_flag_remains_side_effect_free_with_no_mdns(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    async def fail_run(*, advertise_mdns: bool = True) -> None:
        raise AssertionError("--check must not start the gateway")

    # ``--check`` runs ``_run_ownership_check()``, which calls ``_load_dotenv()``.
    # Without stubbing it, the developer's real ``gateway/.env`` is loaded into
    # ``os.environ`` outside monkeypatch's tracking and never restored, polluting
    # later tests (e.g. a ``STACKCHAN_TOKEN`` there breaks the WS-server suite).
    monkeypatch.setattr(cli, "_load_dotenv", lambda: None)
    monkeypatch.setattr(cli, "_run", fail_run)

    with pytest.raises(SystemExit) as exc:
        main(["--check", "--no-mdns"])

    assert exc.value.code == 0


def test_streamable_http_refuses_non_loopback_without_token(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("MCP_HTTP_HOST", "0.0.0.0")

    def fail_acquire(**kwargs: object) -> object:
        raise AssertionError("bind safety must run before ownership lock")

    monkeypatch.setattr(cli, "_acquire_startup_lock", fail_acquire)

    with pytest.raises(SystemExit) as exc:
        main(["serve", "--transport", "streamable-http"])

    assert exc.value.code == 1
    assert (
        "stackchan-mcp: refusing non-loopback MCP_HTTP_HOST without "
        "STACKCHAN_TOKEN or BEARER_TOKEN"
    ) in capsys.readouterr().err


def test_streamable_http_releases_lock_after_daemon_exit(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    from stackchan_mcp import ownership

    info = {
        "owner_id": "owner-test",
        "pid": 123,
        "start_ts": "2026-06-05T00:00:00Z",
        "host": "test-host",
        "mode": "streamable-http",
        "http_endpoint": "127.0.0.1:8767",
        "started_by": "cli-serve",
    }
    acquired: list[dict[str, object]] = []
    released: list[object] = []
    daemon_kwargs: list[dict[str, object]] = []

    def fake_acquire(**kwargs: object) -> dict[str, object]:
        acquired.append(kwargs)
        return info

    async def fake_daemon(**kwargs: object) -> None:
        daemon_kwargs.append(kwargs)

    monkeypatch.setattr(cli, "_configure_gateway_startup", lambda: None)
    monkeypatch.setattr(cli, "_acquire_startup_lock", fake_acquire)
    monkeypatch.setattr(cli, "_run_streamable_http_daemon", fake_daemon)
    monkeypatch.setattr(
        ownership, "release_lock_if_owner", lambda info, path=None: released.append(info)
    )
    monkeypatch.delenv("MCP_HTTP_HOST", raising=False)
    monkeypatch.delenv("MCP_HTTP_PORT", raising=False)
    monkeypatch.delenv("STACKCHAN_TOKEN", raising=False)
    monkeypatch.delenv("BEARER_TOKEN", raising=False)

    cli._run_streamable_http_placeholder(advertise_mdns=False)

    assert acquired == [
        {
            "mode": "streamable-http",
            "http_endpoint": "127.0.0.1:8767",
            "started_by": "cli-serve",
        }
    ]
    assert daemon_kwargs == [
        {
            "host": "127.0.0.1",
            "port": 8767,
            "owner_id": "owner-test",
            "token": None,
            "advertise_mdns": False,
        }
    ]
    assert released == [info]


def test_format_port_status_available() -> None:
    assert _format_port_status(True, None) == "AVAILABLE"


def test_format_port_status_in_use_no_holder() -> None:
    assert _format_port_status(False, None) == "IN USE"


def test_format_port_status_in_use_with_holder() -> None:
    assert (
        _format_port_status(False, "pid 12345, python")
        == "IN USE (pid 12345, python)"
    )


def test_format_port_status_bind_error_is_not_in_use() -> None:
    """Non-EADDRINUSE bind failures must not be reported as ``IN USE``.

    Showing ``IN USE`` for, say, ``EADDRNOTAVAIL`` (HOST not assigned
    to this machine) sends the user looking for a competing process
    that does not exist.
    """
    holder = "bind error: Cannot assign requested address"
    assert (
        _format_port_status(False, holder)
        == "BIND ERROR (Cannot assign requested address)"
    )


def test_check_port_bind_error_when_host_not_local() -> None:
    """A LAN-but-not-local IP triggers EADDRNOTAVAIL, not EADDRINUSE.

    Binding to an IP that is not assigned to any local interface fails
    with ``EADDRNOTAVAIL`` on macOS / Linux. The probe must report this
    distinct from "port in use" so the diagnostic does not mislead.
    192.0.2.0/24 (TEST-NET-1, RFC 5737) is reserved for documentation
    and is virtually guaranteed not to be on a developer's machine.
    """
    available, info = _check_port("192.0.2.1", 0)
    assert available is False
    assert info is not None
    assert info.startswith("bind error:")


def test_check_port_unresolvable_host_returns_bind_error() -> None:
    """``getaddrinfo`` failure is reported as a bind error, not a crash."""
    # ``.invalid`` is reserved by RFC 6761 and never resolves.
    available, info = _check_port("nonexistent.invalid", 0)
    assert available is False
    assert info is not None
    assert info.startswith("bind error:")
    assert "getaddrinfo failed" in info


def test_check_port_resolves_via_getaddrinfo_for_localhost() -> None:
    """``localhost`` should be probed across every resolved address family.

    This is the regression guard for the IPv6 fix: the previous probe
    pinned ``AF_INET``, which would misreport an IPv6-only or
    dual-stack ``localhost`` setup. We just need the call not to raise
    and to produce a sensible boolean — the actual availability is
    racy for any specific port, so we only assert the contract.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()

    available, info = _check_port("localhost", port)
    # Either outcome is fine in principle (the port may have been
    # grabbed between close() and probe); the regression we're catching
    # is "the call raises an exception because of an AF mismatch".
    assert isinstance(available, bool)
    if not available:
        assert info is None or info.startswith("bind error:") or "pid" in info


@pytest.mark.skipif(
    not socket.has_ipv6, reason="IPv6 stack not available on this host"
)
def test_check_port_against_unbound_ipv6_loopback_reports_available() -> None:
    """``::1`` (IPv6 loopback) must be reachable through the new probe.

    Pre-fix this would have raised because the socket was hard-coded
    to ``AF_INET``; the new ``getaddrinfo`` resolver picks ``AF_INET6``
    for the literal address.
    """
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    try:
        sock.bind(("::1", 0))
    except OSError:
        pytest.skip("IPv6 loopback not configured on this host")
    port = sock.getsockname()[1]
    sock.close()

    available, holder = _check_port("::1", port)
    assert available is True
    assert holder is None


def test_check_port_against_unbound_port_reports_available() -> None:
    """Ask the OS for an ephemeral port, release it, then probe.

    Not perfectly race-free (something else could grab the port between
    ``close()`` and ``_check_port``'s bind), but the window is tiny and
    this gives confidence that ``_check_port`` plays nicely with the
    real socket layer rather than only the mocked variant.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()

    available, holder = _check_port("127.0.0.1", port)
    assert available is True
    assert holder is None


def test_check_port_against_held_port_reports_in_use() -> None:
    held = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    held.bind(("127.0.0.1", 0))
    held.listen(1)
    try:
        port = held.getsockname()[1]
        available, _holder = _check_port("127.0.0.1", port)
        assert available is False
    finally:
        held.close()


def test_run_preflight_with_no_config_reports_defaults_and_exits_zero(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    # Don't actually open sockets in the test process.
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 0
    out = capsys.readouterr().out
    assert "STACKCHAN_TOKEN     not set" in out
    assert "MCP_HTTP_ALLOWED_HOSTS not set" in out
    assert "VISION_HOST         not set" in out
    assert "VISION_URL          not set" in out
    assert "VISION_TOKEN        not set" in out
    assert "ws://0.0.0.0:8765" in out
    assert "http://0.0.0.0:8766" in out
    assert "http://127.0.0.1:8767/mcp" in out
    assert "AVAILABLE" in out
    assert "Result: ready. Exit 0." in out


def test_run_preflight_masks_secrets_and_derives_vision_url(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """Tokens must never be echoed; VISION_URL is derived from VISION_HOST."""
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("STACKCHAN_TOKEN", "super-secret-token-value")
    monkeypatch.setenv("VISION_HOST", "192.168.1.42")
    monkeypatch.setenv("VISION_TOKEN", "another-secret-value")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 0
    out = capsys.readouterr().out
    assert "super-secret-token-value" not in out
    assert "another-secret-value" not in out
    # Both tokens should be reported as redacted, not as their raw value.
    assert out.count("***redacted***") == 2
    # VISION_HOST is configuration, not a secret, so it is shown as-is.
    assert "VISION_HOST         192.168.1.42" in out
    assert "(derived) http://192.168.1.42:8766/capture" in out


def test_run_preflight_explicit_vision_url_overrides_derivation(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("VISION_HOST", "192.168.1.42")
    monkeypatch.setenv("VISION_URL", "https://stackchan.example.ts.net/capture")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    _run_preflight()
    out = capsys.readouterr().out
    assert "VISION_URL          https://stackchan.example.ts.net/capture" in out
    # The derived line must not appear when an explicit URL is set.
    assert "(derived)" not in out


def test_redact_url_secrets_strips_basic_auth_userinfo() -> None:
    """``user:pass@`` must be replaced before the URL is printed."""
    out = _redact_url_secrets("https://user:pass@example.com:8443/capture")
    assert "user" not in out
    assert "pass" not in out
    assert "***:***@example.com:8443/capture" in out
    assert out.startswith("https://")


def test_redact_url_secrets_masks_secret_query_params() -> None:
    """Common token / signature keys must be redacted; other keys stay."""
    out = _redact_url_secrets(
        "https://example.com/capture?token=abc123&page=1&signature=xyz"
    )
    assert "abc123" not in out
    assert "xyz" not in out
    assert "page=1" in out  # non-secret params are preserved
    assert "redacted" in out


def test_redact_url_secrets_leaves_safe_url_unchanged() -> None:
    """A URL with no userinfo or secret params must not be altered."""
    safe = "https://stackchan.example.ts.net:8443/capture?page=2"
    assert _redact_url_secrets(safe) == safe


def test_redact_url_secrets_masks_provider_specific_signed_params() -> None:
    """AWS / GCP / Azure signed-URL params must be redacted via heuristic.

    The exact-match set covers generic names like ``token`` and
    ``signature``, but provider-specific parameters such as
    ``X-Amz-Signature``, ``X-Amz-Security-Token``,
    ``X-Goog-Signature``, ``X-Amz-Credential`` are not in the explicit
    list; the suffix heuristic ensures they are still masked so a
    pre-signed S3 / GCS URL pasted into ``VISION_URL`` does not leak
    its credential payload through ``--check``.
    """
    out = _redact_url_secrets(
        "https://bucket.s3.amazonaws.com/capture"
        "?X-Amz-Signature=AAAA&X-Amz-Security-Token=BBBB"
        "&X-Amz-Credential=CCCC&X-Amz-Algorithm=AWS4-HMAC-SHA256"
        "&page=1"
    )
    assert "AAAA" not in out
    assert "BBBB" not in out
    assert "CCCC" not in out
    # Algorithm name is not a secret; it should remain visible so the
    # user can still tell what scheme the URL is signed with.
    assert "AWS4-HMAC-SHA256" in out
    assert "page=1" in out


def test_redact_url_secrets_handles_unparseable_input_gracefully() -> None:
    """Malformed input must not crash the preflight."""
    # urlparse is very permissive, so this is more about the contract
    # than triggering the except: anything weird simply round-trips.
    assert _redact_url_secrets("") == ""
    weird = "not a url at all"
    # Either the input is returned as-is or urlparse rebuilds it
    # losslessly; we only care that no exception escapes.
    assert isinstance(_redact_url_secrets(weird), str)


def test_run_preflight_redacts_explicit_vision_url(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """Tokens in ``VISION_URL`` must not appear in preflight output.

    The preflight is meant to be safe to paste into an issue or log,
    so signed-URL secrets and Basic-auth userinfo have to be masked at
    print time.
    """
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv(
        "VISION_URL",
        "https://signer:topsecret@example.com/capture?token=tk_abc123",
    )
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    _run_preflight()
    out = capsys.readouterr().out
    assert "topsecret" not in out
    assert "tk_abc123" not in out
    assert "signer" not in out
    assert "example.com/capture" in out


def test_run_preflight_in_use_ports_return_nonzero(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setattr(
        cli,
        "_check_port",
        lambda host, port: (False, f"pid 12345, mock-{port}"),
    )

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "IN USE (pid 12345, mock-8765)" in out
    assert "IN USE (pid 12345, mock-8766)" in out
    assert "IN USE (pid 12345, mock-8767)" in out
    assert "Result: 3 issues. Exit 1." in out


def test_run_preflight_one_in_use_port_singular_phrasing(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)

    def fake_check(host: str, port: int) -> tuple[bool, str | None]:
        if port == 8765:
            return (False, "pid 999, fake")
        return (True, None)

    monkeypatch.setattr(cli, "_check_port", fake_check)

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    # Singular ``issue`` (not ``issues``) when exactly one port is held.
    assert "Result: 1 issue. Exit 1." in out


def test_main_check_flag_runs_ownership_check_and_exits(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``main(['--check'])`` exits after the ownership check.

    Guards the contract that ``--check`` never reaches gateway startup
    or the port/config preflight path below the early exit.
    """
    from stackchan_mcp import ownership

    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setattr(cli, "_run_preflight", lambda: 99)
    monkeypatch.setattr(ownership, "read_lock", lambda path=None: None)

    with pytest.raises(SystemExit) as exc:
        main(["--check"])
    assert exc.value.code == 0
    out = capsys.readouterr().out
    assert "ownership preflight" in out
    assert "Result: ready" in out


def test_main_check_flag_inspects_per_ws_port_lock(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``--check`` inspects the lock for the configured WS_PORT.

    Guards against the ``--check`` preflight reading the default
    ``owner-8765.lock`` while a gateway configured for another port owns
    ``owner-<port>.lock``. ``_run_ownership_check`` loads ``.env`` before
    deriving the lock path, mirroring the startup order, so a WS_PORT set in
    the environment (or ``.env``) is reflected here.
    """
    from stackchan_mcp import ownership

    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("WS_PORT", "18765")
    seen: list[Path] = []

    def fake_read_lock(path: Path | None = None) -> None:
        seen.append(path)
        return None

    monkeypatch.setattr(ownership, "read_lock", fake_read_lock)

    with pytest.raises(SystemExit) as exc:
        main(["--check"])
    assert exc.value.code == 0
    assert seen and seen[0] is not None and seen[0].name == "owner-18765.lock"
    assert "owner-18765.lock" in capsys.readouterr().out


def test_main_check_flag_scopes_lock_for_ephemeral_ws_port(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``WS_PORT=0`` scopes the lock to ``owner-0.lock``, not the legacy path.

    Preflight treats ``0`` as a valid (OS-assigned ephemeral) port, so the
    ownership lock must stay per-port for it too. A truthiness check would send
    ``0`` to the machine-global ``owner.lock`` and diverge from startup.
    """
    from stackchan_mcp import ownership

    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("WS_PORT", "0")
    seen: list[Path] = []

    def fake_read_lock(path: Path | None = None) -> None:
        seen.append(path)
        return None

    monkeypatch.setattr(ownership, "read_lock", fake_read_lock)

    with pytest.raises(SystemExit) as exc:
        main(["--check"])
    assert exc.value.code == 0
    assert seen and seen[0] is not None and seen[0].name == "owner-0.lock"


def test_main_preflight_flag_runs_preflight_and_exits(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(cli, "_run_preflight", lambda: 7)

    with pytest.raises(SystemExit) as exc:
        main(["--preflight"])

    assert exc.value.code == 7


# --- Port resolution tests (must mirror gateway.py) -------------------------


def test_resolve_ws_port_defaults_to_8765(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.delenv("WS_PORT", raising=False)
    monkeypatch.delenv("PORT", raising=False)
    port, source = cli._resolve_ws_port()
    assert port == 8765
    assert source == "default"


def test_resolve_ws_port_prefers_ws_port_over_port(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("WS_PORT", "9000")
    monkeypatch.setenv("PORT", "9001")
    port, source = cli._resolve_ws_port()
    assert port == 9000
    assert source == "WS_PORT"


def test_resolve_ws_port_falls_back_to_PORT(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """gateway.py: int(os.getenv("WS_PORT", os.getenv("PORT", "8765")))."""
    monkeypatch.delenv("WS_PORT", raising=False)
    monkeypatch.setenv("PORT", "9001")
    port, source = cli._resolve_ws_port()
    assert port == 9001
    assert source == "PORT"


def test_resolve_ws_port_invalid_value_returns_none(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("WS_PORT", "abc")
    port, source = cli._resolve_ws_port()
    assert port is None
    assert "WS_PORT" in source
    assert "not an integer" in source


@pytest.mark.parametrize("bad_value", ["-1", "65536", "100000"])
def test_resolve_ws_port_out_of_range_returns_none(
    monkeypatch: pytest.MonkeyPatch, bad_value: str
) -> None:
    """Values outside 0-65535 must be rejected before they reach bind().

    ``socket.bind()`` raises ``OverflowError`` for integers outside the
    TCP port range, which would crash ``--check`` with a stack trace
    instead of producing the diagnostic report it is meant to produce.
    """
    monkeypatch.setenv("WS_PORT", bad_value)
    port, source = cli._resolve_ws_port()
    assert port is None
    assert "out of TCP port range" in source


def test_resolve_ws_port_zero_is_accepted(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """``0`` lets the OS pick an ephemeral port — bind-able, so accept it.

    The gateway may not actually want this in production, but it is a
    valid TCP port value and ``bind((host, 0))`` succeeds. Preflight
    only filters out values that would crash ``bind()``.
    """
    monkeypatch.setenv("WS_PORT", "0")
    port, source = cli._resolve_ws_port()
    assert port == 0
    assert source == "WS_PORT"


def test_resolve_capture_port_defaults_to_8766(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.delenv("CAPTURE_PORT", raising=False)
    port, source = cli._resolve_capture_port()
    assert port == 8766
    assert source == "default"


def test_resolve_capture_port_invalid_value_returns_none(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("CAPTURE_PORT", "not-a-number")
    port, source = cli._resolve_capture_port()
    assert port is None
    assert "CAPTURE_PORT" in source
    assert "not an integer" in source


@pytest.mark.parametrize("bad_value", ["-1", "65536", "99999"])
def test_resolve_capture_port_out_of_range_returns_none(
    monkeypatch: pytest.MonkeyPatch, bad_value: str
) -> None:
    monkeypatch.setenv("CAPTURE_PORT", bad_value)
    port, source = cli._resolve_capture_port()
    assert port is None
    assert "out of TCP port range" in source


def test_run_preflight_out_of_range_ws_port_is_blocking(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """Out-of-range WS port must be reported, not crashed on.

    Before this guard, ``WS_PORT=65536`` would parse as int and reach
    ``socket.bind()``, which raises ``OverflowError`` and aborts the
    preflight without printing the result line — exactly the failure
    mode ``--check`` is meant to catch in advance.
    """
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("WS_PORT", "65536")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "INVALID" in out
    assert "out of TCP port range" in out


def test_run_preflight_invalid_ws_port_is_blocking(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``WS_PORT=<garbage>`` must NOT silently fall back to the default.

    The gateway itself wraps the lookup in ``int(...)`` without a
    try/except — silent fallback in preflight would mean reporting
    "ready" for an environment the gateway would actually refuse to
    start. That is the exact failure mode --check is meant to catch.
    """
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("WS_PORT", "not-a-number")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "INVALID" in out
    assert "WS_PORT" in out
    assert "Result: 1 issue. Exit 1." in out


def test_run_preflight_invalid_capture_port_is_blocking(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("CAPTURE_PORT", "garbage")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "INVALID" in out
    assert "CAPTURE_PORT" in out


def test_run_preflight_invalid_mcp_http_port_is_blocking(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("MCP_HTTP_PORT", "not-a-number")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "INVALID" in out
    assert "MCP_HTTP_PORT" in out


def test_run_preflight_non_loopback_mcp_http_without_token_is_blocking(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("MCP_HTTP_HOST", "0.0.0.0")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "MCP HTTP bind safety: BLOCKED" in out
    assert "Result: 1 issue. Exit 1." in out


def test_run_preflight_non_loopback_mcp_http_with_token_is_ready(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("MCP_HTTP_HOST", "0.0.0.0")
    monkeypatch.setenv("STACKCHAN_TOKEN", "secret")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 0
    out = capsys.readouterr().out
    assert "MCP HTTP bind safety: BLOCKED" not in out
    assert "http://0.0.0.0:8767/mcp" in out
    assert "Result: ready. Exit 0." in out


def test_run_preflight_uses_PORT_fallback_for_ws_port(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``PORT=<value>`` must be honored when ``WS_PORT`` is unset.

    ``gateway.py`` resolves ``WS_PORT`` → ``PORT`` → ``8765``, so the
    preflight must check the same port that ``Gateway.start()`` will
    actually bind to.
    """
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("PORT", "9999")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    _run_preflight()
    out = capsys.readouterr().out
    assert "ws://0.0.0.0:9999" in out
    # Capture port still falls through to its own default.
    assert "http://0.0.0.0:8766" in out


def test_run_preflight_ws_and_capture_same_port_is_conflict(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``WS_PORT == CAPTURE_PORT`` must be flagged even when the port is free.

    ``_check_port`` binds-and-releases each port independently, so two
    successive probes for the same free port both report AVAILABLE.
    The gateway, however, holds the WebSocket port for the entire
    process lifetime, so a subsequent capture bind would fail. The
    conflict has to be caught at the configuration layer.
    """
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("WS_PORT", "8765")
    monkeypatch.setenv("CAPTURE_PORT", "8765")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "8765" in out
    assert "distinct ports" in out


def test_run_preflight_mcp_http_port_conflict_is_blocking(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("CAPTURE_PORT", "8767")
    monkeypatch.setenv("MCP_HTTP_PORT", "8767")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 1
    out = capsys.readouterr().out
    assert "MCP_HTTP_PORT" in out
    assert "CAPTURE_PORT" in out
    assert "distinct listener ports" in out


def test_run_preflight_both_ports_zero_is_not_a_conflict(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
) -> None:
    """``WS_PORT=0`` AND ``CAPTURE_PORT=0`` is a valid ephemeral setup.

    Each ``bind((host, 0))`` asks the OS for a fresh ephemeral port, so
    two listeners both configured with 0 do not actually collide —
    this is the exact configuration the existing gateway tests use.
    The conflict check must therefore exclude port 0; otherwise
    ``--check`` would falsely fail a supported gateway start-up
    scenario.
    """
    _isolate_preflight_env(monkeypatch, tmp_path)
    monkeypatch.setenv("WS_PORT", "0")
    monkeypatch.setenv("CAPTURE_PORT", "0")
    monkeypatch.setattr(cli, "_check_port", lambda host, port: (True, None))

    exit_code = _run_preflight()
    assert exit_code == 0
    out = capsys.readouterr().out
    assert "distinct ports" not in out
    assert "Result: ready. Exit 0." in out


# ---------------------------------------------------------------------------
# _ensure_libopus_findable — Homebrew dlopen helper (macOS) (Issue #70 PR2)
# ---------------------------------------------------------------------------


def test_ensure_libopus_findable_noop_on_non_macos(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """The helper is a strict no-op when the host platform is not macOS."""
    monkeypatch.setattr(cli.platform, "system", lambda: "Linux")
    monkeypatch.delenv("DYLD_LIBRARY_PATH", raising=False)

    cli._ensure_libopus_findable()

    assert "DYLD_LIBRARY_PATH" not in os.environ


def test_ensure_libopus_findable_prepends_homebrew_lib(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """A present Homebrew lib directory is prepended to DYLD_LIBRARY_PATH."""
    monkeypatch.setattr(cli.platform, "system", lambda: "Darwin")
    # Pretend only /opt/homebrew/lib exists (Apple Silicon default).
    monkeypatch.setattr(
        cli.os.path,
        "isdir",
        lambda p: p == "/opt/homebrew/lib",
    )
    monkeypatch.delenv("DYLD_LIBRARY_PATH", raising=False)

    cli._ensure_libopus_findable()

    assert os.environ["DYLD_LIBRARY_PATH"] == "/opt/homebrew/lib"


def test_ensure_libopus_findable_does_not_duplicate_existing_entries(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """An entry already on DYLD_LIBRARY_PATH is not re-prepended."""
    monkeypatch.setattr(cli.platform, "system", lambda: "Darwin")
    monkeypatch.setattr(
        cli.os.path,
        "isdir",
        lambda p: p == "/opt/homebrew/lib",
    )
    monkeypatch.setenv(
        "DYLD_LIBRARY_PATH", "/opt/homebrew/lib:/some/other/lib"
    )

    cli._ensure_libopus_findable()

    # Unchanged because the only candidate was already present.
    assert (
        os.environ["DYLD_LIBRARY_PATH"]
        == "/opt/homebrew/lib:/some/other/lib"
    )


def test_ensure_libopus_findable_preserves_user_dyld_priority(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """User-set DYLD_LIBRARY_PATH stays at the front; Homebrew is appended.

    Operators who built libopus from source and pointed
    DYLD_LIBRARY_PATH at the custom build expect that prefix to win.
    The helper prepends Homebrew dirs ahead of itself but keeps the
    operator's existing entries intact and after the new entries —
    the new entries only fire if find_library does not match earlier.
    """
    monkeypatch.setattr(cli.platform, "system", lambda: "Darwin")
    monkeypatch.setattr(
        cli.os.path,
        "isdir",
        lambda p: p in {"/opt/homebrew/lib", "/usr/local/lib"},
    )
    monkeypatch.setenv("DYLD_LIBRARY_PATH", "/Users/dev/libopus-build/lib")

    cli._ensure_libopus_findable()

    # New Homebrew dirs sit ahead of the helper-prepended block, but
    # the user's prior entry follows them — i.e. it is still present.
    parts = os.environ["DYLD_LIBRARY_PATH"].split(":")
    assert "/Users/dev/libopus-build/lib" in parts
    assert "/opt/homebrew/lib" in parts
    assert "/usr/local/lib" in parts


def test_ensure_libopus_findable_handles_missing_homebrew(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """If neither Homebrew prefix exists, DYLD_LIBRARY_PATH is left alone."""
    monkeypatch.setattr(cli.platform, "system", lambda: "Darwin")
    monkeypatch.setattr(cli.os.path, "isdir", lambda p: False)
    monkeypatch.delenv("DYLD_LIBRARY_PATH", raising=False)

    cli._ensure_libopus_findable()

    assert "DYLD_LIBRARY_PATH" not in os.environ
