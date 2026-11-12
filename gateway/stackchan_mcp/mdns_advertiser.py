"""mDNS/DNS-SD advertisement for the StackChan WebSocket gateway."""

from __future__ import annotations

import asyncio
import ipaddress
import logging
import socket
import uuid
from dataclasses import dataclass
from fnmatch import fnmatchcase
from typing import Any

logger = logging.getLogger(__name__)

SERVICE_TYPE = "_stackchan-mcp._tcp.local."
DEFAULT_INSTANCE = "stackchan-mcp"
SERVICE_NAME = f"{DEFAULT_INSTANCE}.{SERVICE_TYPE}"
# Stable through address refreshes, distinct across processes and restarts so
# stale/renamed instances cannot merge another gateway's A records.
_GATEWAY_ID = uuid.uuid4().hex
TXT_VERSION = "1"

# Private (RFC1918) IPv4 ranges. Addresses inside these ranges are the most
# likely to be reachable from a same-LAN device, so they are advertised first.
# Anything outside is still advertised, just ordered after these.
_PRIVATE_NETWORKS = (
    ipaddress.ip_network("192.168.0.0/16"),
    ipaddress.ip_network("10.0.0.0/8"),
    ipaddress.ip_network("172.16.0.0/12"),
)

# Only prefixes shorter than /31 have a distinct network and broadcast address.
# A /31 (point-to-point) or /32 (host) address must never be treated as a
# network or broadcast address, since that would drop a legitimate host IP.
_MAX_NETWORK_BROADCAST_PREFIX = 30

# Keep physical LANs (including bridge0); only known VM bridges and VPN tunnels
# are excluded. Address ranges alone cannot distinguish these from home LANs.
_NON_LAN_INTERFACE_PATTERNS = (
    "docker*",
    "br-*",
    "bridge1[0-9][0-9]",
    "vmnet*",
    "vboxnet*",
    "virbr*",
    "veth*",
    "utun*",
    "tun*",
    "wg*",
    "tailscale*",
)


@dataclass(frozen=True)
class MdnsAdvertisement:
    """Resolved service advertisement parameters."""

    service_type: str
    service_name: str
    server: str
    port: int
    path: str
    properties: dict[str, str]
    parsed_addresses: list[str]


def _load_zeroconf_classes() -> tuple[type[Any], type[Any]]:
    from zeroconf import ServiceInfo
    from zeroconf.asyncio import AsyncZeroconf

    return AsyncZeroconf, ServiceInfo


def _is_usable_ipv4(address: str) -> bool:
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return (
        ip.version == 4
        and not ip.is_unspecified
        and not ip.is_loopback
        and not ip.is_multicast
    )


def _is_network_or_broadcast_address(address: str, prefix: int | None) -> bool:
    """Return ``True`` when ``address`` is the subnet network or broadcast address.

    Requires the subnet prefix: without it (e.g. the socket-based source) the
    network/broadcast endpoints cannot be derived, so callers pass ``None`` and
    the address is kept. Host prefixes (/31, /32) have no distinct network or
    broadcast address and are never excluded here.
    """
    if prefix is None or prefix > _MAX_NETWORK_BROADCAST_PREFIX:
        return False
    try:
        interface = ipaddress.ip_interface(f"{address}/{prefix}")
    except ValueError:
        return False
    network = interface.network
    return interface.ip in (network.network_address, network.broadcast_address)


def _is_private_lan_address(address: str) -> bool:
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return any(ip in network for network in _PRIVATE_NETWORKS)


def _lan_reachability_tier(address: str) -> int:
    """Sort key: private (RFC1918) LAN addresses first, everything else after.

    Returns a 2-value tier so a stable sort preserves the original enumeration
    order within each tier.
    """
    return 0 if _is_private_lan_address(address) else 1


def _select_advertised_addresses(
    candidates: list[tuple[str, int | None, str | None]],
    *,
    preferred_address: str | None = None,
) -> list[str]:
    """Filter, de-duplicate and order candidate addresses for advertisement.

    Retain every LAN and unknown interface, excluding known VM/VPN interfaces.
    If filtering removes everything, fall back to all usable addresses. The
    default route leads only if retained; DNS A record order is not guaranteed.
    """
    seen: set[str] = set()
    usable: list[str] = []
    for address, prefix, _interface in candidates:
        if address in seen or not _is_usable_ipv4(address):
            continue
        if _is_network_or_broadcast_address(address, prefix):
            continue
        seen.add(address)
        usable.append(address)
    excluded = {
        address
        for address, _prefix, interface in candidates
        if interface
        and any(fnmatchcase(interface.lower(), pattern) for pattern in _NON_LAN_INTERFACE_PATTERNS)
    }
    retained = [address for address in usable if address not in excluded] or usable
    retained.sort(
        key=lambda address: (
            address != preferred_address,
            _lan_reachability_tier(address),
        )
    )
    return retained


def _is_wildcard_host(host: str) -> bool:
    try:
        ip = ipaddress.ip_address(host)
    except ValueError:
        return host in {"", "*"}
    return ip.is_unspecified


def _build_service_hostname(gateway_id: str) -> str:
    """Return a service-specific mDNS hostname for the SRV record.

    Keep each gateway's A records separate, including after instance renaming,
    without overlapping the system's Bonjour LocalHostName.
    """
    return f"{DEFAULT_INSTANCE}-{gateway_id}.local."


def _iter_ifaddr_ipv4_addresses() -> list[tuple[str, int | None, str | None]]:
    """Enumerate host IPv4 addresses with subnet prefix and interface name.

    The prefix (``ip.network_prefix``) lets the caller drop network/broadcast
    addresses; it is ``None`` only if ifaddr reports a non-integer prefix.
    """
    try:
        import ifaddr
    except ImportError:
        return []

    addresses: list[tuple[str, int | None, str | None]] = []
    for adapter in ifaddr.get_adapters():
        for ip in adapter.ips:
            if not isinstance(ip.ip, str):
                continue
            prefix = ip.network_prefix if isinstance(ip.network_prefix, int) else None
            addresses.append((ip.ip, prefix, adapter.name))
    return addresses


def _iter_socket_ipv4_addresses() -> list[tuple[str, int | None, str | None]]:
    """Enumerate host IPv4 addresses via socket resolution.

    This source carries no subnet prefix or interface name. Unmatched entries
    are retained for compatibility; matching ifaddr metadata is inherited.
    """
    addresses: set[str] = set()
    hostnames = {socket.gethostname(), socket.getfqdn()}

    for hostname in hostnames:
        try:
            infos = socket.getaddrinfo(hostname, None, socket.AF_INET)
        except socket.gaierror:
            continue
        for _family, _socktype, _proto, _canonname, sockaddr in infos:
            addresses.add(sockaddr[0])

    return [(address, None, None) for address in sorted(addresses)]


def _primary_ipv4_address() -> str | None:
    """Find the default-route IPv4; UDP connect selects it without sending data."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return None
    finally:
        sock.close()


def _enumerate_usable_ipv4_addresses() -> list[str]:
    ifaddr_entries = _iter_ifaddr_ipv4_addresses()
    socket_entries = _iter_socket_ipv4_addresses()
    primary = _primary_ipv4_address()
    if primary is not None:
        socket_entries.append((primary, None, None))

    # The socket-based source carries no subnet prefix, so on its own it cannot
    # exclude network/broadcast addresses. When the same address also appears
    # in the ifaddr source (which does carry a prefix), adopt that prefix so
    # ``_is_network_or_broadcast_address`` can recognise and drop the entry.
    # Without this, a host whose ``getaddrinfo``-resolved set includes an
    # interface's subnet network base (which can happen when an interface ends
    # up with its own subnet's network address as its host IP) would be
    # advertised and then crash the zeroconf socket with ``EADDRNOTAVAIL``.
    prefix_by_address = {
        address: prefix for address, prefix, _interface in ifaddr_entries if prefix is not None
    }
    interface_by_address = {
        address: interface
        for address, _prefix, interface in ifaddr_entries
        if interface is not None
    }
    enriched_socket_entries = [
        (
            address,
            prefix_by_address.get(address, prefix),
            interface_by_address.get(address, interface),
        )
        for address, prefix, interface in socket_entries
    ]

    return _select_advertised_addresses(
        [*ifaddr_entries, *enriched_socket_entries], preferred_address=primary
    )


def _resolve_concrete_host_ipv4_addresses(host: str) -> list[str]:
    try:
        ip = ipaddress.ip_address(host)
    except ValueError:
        try:
            infos = socket.getaddrinfo(host, None, socket.AF_INET)
        except socket.gaierror:
            return []
        addresses = [sockaddr[0] for *_unused, sockaddr in infos]
    else:
        addresses = [str(ip)]

    # A concrete HOST carries no subnet prefix, so network/broadcast addresses
    # cannot be derived; pair each address with ``None`` to keep them.
    return _select_advertised_addresses([(address, None, None) for address in addresses])


def _resolve_addresses_for_host(host: str) -> list[str]:
    """Resolve advertised addresses for ``host`` using the same logic as
    :func:`build_advertisement`.

    Wildcard hosts (``0.0.0.0`` / ``*`` / ``""``) retain all LAN IPv4 addresses,
    preferring the default route only among retained addresses.
    Concrete hosts only resolve
    addresses for that specific host. The refresh loop calls this helper so
    its comparison set matches what a fresh ``build_advertisement(...)``
    would actually register — without this, a host started with a concrete
    HOST in a multi-NIC / Tailscale environment would see every poll as an
    "address change" against an unrelated extra interface and churn the
    registration on every refresh interval.
    """
    return (
        _enumerate_usable_ipv4_addresses()
        if _is_wildcard_host(host)
        else _resolve_concrete_host_ipv4_addresses(host)
    )


def build_advertisement(
    *,
    host: str,
    port: int,
    path: str = "/",
) -> MdnsAdvertisement | None:
    """Resolve advertisement parameters, or ``None`` if they would be unusable."""
    if port <= 0 or port > 65535:
        logger.warning(
            "mDNS advertisement skipped: WebSocket port %s is not publishable",
            port,
        )
        return None

    normalized_path = path if path.startswith("/") else f"/{path}"
    addresses = _resolve_addresses_for_host(host)
    if not addresses:
        logger.warning(
            "mDNS advertisement skipped: no usable non-loopback IPv4 address "
            "found for HOST=%s",
            host,
        )
        return None

    return MdnsAdvertisement(
        service_type=SERVICE_TYPE,
        service_name=SERVICE_NAME,
        server=_build_service_hostname(_GATEWAY_ID),
        port=port,
        path=normalized_path,
        properties={"path": normalized_path, "version": TXT_VERSION},
        parsed_addresses=addresses,
    )


class MdnsAdvertiser:
    """Registers the gateway's WebSocket endpoint via mDNS/DNS-SD."""

    def __init__(self, *, refresh_interval: float = 30.0) -> None:
        if refresh_interval < 10.0 or refresh_interval > 300.0:
            raise ValueError("refresh_interval must be between 10.0 and 300.0 seconds")
        self._lock: asyncio.Lock = asyncio.Lock()
        self._zeroconf: Any | None = None
        self._service_info: Any | None = None
        self._refresh_task: asyncio.Task | None = None
        self._last_advertised_addresses: tuple[str, ...] | None = None
        self._refresh_interval: float = refresh_interval
        self._start_args: dict[str, Any] | None = None

    async def start(self, *, host: str, port: int, path: str = "/") -> None:
        task = self._refresh_task
        if task is not None and task is not asyncio.current_task():
            task.cancel()
            try:
                await asyncio.wait_for(task, timeout=1.0)
            except (asyncio.CancelledError, TimeoutError):
                pass

        async with self._lock:
            self._refresh_task = None
            if self._zeroconf is not None:
                await self._close_zeroconf_locked()
            self._start_args = {"host": host, "port": port, "path": path}
            try:
                advertisement = await self._start_locked(host=host, port=port, path=path)
            except Exception:
                self._start_args = None
                self._last_advertised_addresses = None
                raise
            if advertisement is None:
                self._start_args = None
                self._last_advertised_addresses = None
                return
            self._last_advertised_addresses = tuple(advertisement.parsed_addresses)
            self._refresh_task = asyncio.create_task(self._refresh_loop())

    async def _start_locked(
        self, *, host: str, port: int, path: str = "/"
    ) -> MdnsAdvertisement | None:
        advertisement = build_advertisement(host=host, port=port, path=path)
        if advertisement is None:
            return None

        await self._register_advertisement_locked(advertisement)
        return advertisement

    async def _register_advertisement_locked(
        self, advertisement: MdnsAdvertisement
    ) -> None:
        AsyncZeroconf, ServiceInfo = _load_zeroconf_classes()
        # Constrain zeroconf to the IPv4 interfaces we actually advertise on.
        # The default ``InterfaceChoice.All`` makes zeroconf bind a socket on
        # every host IPv4 address it can find, which on a host with several
        # interfaces can include addresses the kernel refuses ``sendto`` on
        # (the engine then never finishes starting, and the gateway hangs in
        # ``async_wait_for_start``). Passing the same set of addresses we put
        # into the SRV record keeps zeroconf in sync with our advertisement
        # and skips the unusable interfaces entirely.
        zeroconf = AsyncZeroconf(interfaces=advertisement.parsed_addresses)
        info = ServiceInfo(
            advertisement.service_type,
            advertisement.service_name,
            port=advertisement.port,
            properties=advertisement.properties,
            server=advertisement.server,
            parsed_addresses=advertisement.parsed_addresses,
        )
        try:
            await zeroconf.async_register_service(info, allow_name_change=True)
        except BaseException:
            # Catch BaseException (not just Exception) so that an
            # ``asyncio.CancelledError`` arriving mid-registration —
            # e.g. an external ``stop()`` or a double-``start()`` racing
            # this registration — still closes the partially-constructed
            # ``AsyncZeroconf``. Otherwise the bound multicast sockets
            # and any partial registration would leak past the cancelled
            # task. ``raise`` preserves the cancellation semantics for
            # the surrounding ``async with self._lock`` caller.
            await zeroconf.async_close()
            raise
        self._zeroconf = zeroconf
        self._service_info = info
        registered_name = getattr(info, "name", advertisement.service_name)
        if registered_name != advertisement.service_name:
            logger.warning(
                "mDNS service registered under a modified name %s (requested %s). "
                "A previous gateway instance may not have shut down cleanly and its "
                "registration is still visible on the network. The ESP32 still "
                "discovers this gateway by service type, so auto-discovery keeps "
                "working; the stale entry clears when its mDNS TTL expires.",
                registered_name,
                advertisement.service_name,
            )
        logger.info(
            "mDNS advertising %s server=%s on port %d with addresses %s reason=register",
            registered_name,
            advertisement.server,
            advertisement.port,
            ", ".join(advertisement.parsed_addresses),
        )

    async def stop(self) -> None:
        task = self._refresh_task
        if task is not None and task is not asyncio.current_task():
            task.cancel()
            try:
                await asyncio.wait_for(task, timeout=1.0)
            except (asyncio.CancelledError, TimeoutError):
                pass

        async with self._lock:
            await self._close_zeroconf_locked()
            self._refresh_task = None
            self._last_advertised_addresses = None
            self._start_args = None

    async def _close_zeroconf_locked(self) -> None:
        zeroconf = self._zeroconf
        info = self._service_info
        self._zeroconf = None
        self._service_info = None

        if zeroconf is None:
            return
        try:
            if info is not None:
                await zeroconf.async_unregister_service(info)
        finally:
            await zeroconf.async_close()

    async def _reconfigure(self) -> None:
        async with self._lock:
            if self._start_args is None:
                return
            old_addresses = self._last_advertised_addresses
            advertisement = build_advertisement(**self._start_args)
            if advertisement is None:
                logger.info(
                    "mDNS reconfigure skipped: build_advertisement returned None "
                    "(transient empty address state); refresh loop continues "
                    "reason=transient_empty_address_state old=%s",
                    old_addresses,
                )
                return

            # Clear the cached advertised set BEFORE closing so that even a
            # failure inside ``_close_zeroconf_locked()`` itself (e.g. the
            # old interface has vanished mid-cycle and ``async_unregister``
            # / ``async_close`` raise) still leaves the refresh loop in a
            # "must retry" state. If the subsequent close raises, or the
            # re-registration raises, or the host IP later reverts to
            # ``old_addresses``, the refresh loop's
            # ``current != _last_advertised_addresses`` check would
            # otherwise compare against the stale value and stay quiet —
            # the advertisement would remain dead until a manual restart.
            # Setting this to ``None`` here guarantees the next refresh
            # tick observes a divergence and retries registration
            # regardless of which address state the host happens to be in.
            self._last_advertised_addresses = None
            await self._close_zeroconf_locked()
            await self._register_advertisement_locked(advertisement)
            new_addresses = tuple(advertisement.parsed_addresses)
            self._last_advertised_addresses = new_addresses
            registered_name = getattr(
                self._service_info,
                "name",
                advertisement.service_name,
            )
            logger.info(
                "mDNS reconfigured: reason=address_changed old=%s new=%s "
                "registered_name=%s",
                old_addresses,
                new_addresses,
                registered_name,
            )

    async def _refresh_loop(self) -> None:
        while True:
            try:
                await asyncio.sleep(self._refresh_interval)
                start_args = self._start_args
                if start_args is None:
                    continue
                host = start_args["host"]
                current_addresses = tuple(_resolve_addresses_for_host(host))
                old_addresses = self._last_advertised_addresses
                if current_addresses == old_addresses:
                    continue

                logger.info(
                    "mDNS refresh observed address change: reason=address_changed "
                    "old=%s new=%s debounce=started",
                    old_addresses,
                    current_addresses,
                )
                await asyncio.sleep(self._refresh_interval)
                start_args = self._start_args
                if start_args is None:
                    continue
                host = start_args["host"]
                confirmed_addresses = tuple(_resolve_addresses_for_host(host))
                if confirmed_addresses != current_addresses:
                    logger.info(
                        "mDNS refresh dropped transient address change: "
                        "reason=debounce_mismatch old=%s first=%s second=%s",
                        old_addresses,
                        current_addresses,
                        confirmed_addresses,
                    )
                    continue

                await self._reconfigure()
            except asyncio.CancelledError:
                return
            except Exception:
                logger.exception("mDNS refresh loop error; continuing")
                continue
