"""Tests for mDNS/DNS-SD gateway advertisement."""

from __future__ import annotations

import asyncio

import pytest

from stackchan_mcp import mdns_advertiser as mdns
from stackchan_mcp.mdns_advertiser import MdnsAdvertiser, build_advertisement


@pytest.fixture(autouse=True)
def no_host_default_route(monkeypatch: pytest.MonkeyPatch) -> None:
    # Address-selection tests must not depend on the runner's network.
    monkeypatch.setattr(mdns, "_primary_ipv4_address", lambda: None)


def test_service_type_and_txt_defaults() -> None:
    advertisement = build_advertisement(
        host="192.0.2.10",
        port=8765,
        path="/",
    )

    assert advertisement is not None
    assert advertisement.service_type == "_stackchan-mcp._tcp.local."
    assert advertisement.service_name == "stackchan-mcp._stackchan-mcp._tcp.local."
    assert advertisement.port == 8765
    assert advertisement.properties == {"path": "/", "version": "1"}
    assert advertisement.parsed_addresses == ["192.0.2.10"]


def test_service_hostname_is_service_specific() -> None:
    assert mdns._build_service_hostname("a" * 32) == f"stackchan-mcp-{'a' * 32}.local."
    assert mdns._build_service_hostname("a" * 32) != mdns._build_service_hostname("b" * 32)


def test_same_named_gateways_have_separate_srv_address_owners(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(mdns, "_GATEWAY_ID", "a" * 32)
    first = build_advertisement(host="192.0.2.10", port=8765)
    refreshed = build_advertisement(host="192.0.2.11", port=8765)
    monkeypatch.setattr(mdns, "_GATEWAY_ID", "b" * 32)
    second = build_advertisement(host="192.0.2.20", port=8765)
    assert first is not None and refreshed is not None and second is not None
    assert first.service_name == second.service_name
    assert first.server == refreshed.server
    assert first.server != second.server
    # Even if DNS-SD renames the second instance, its SRV target owns only
    # that gateway's A records; no shared owner can merge the two address sets.
    records = {
        first.server: first.parsed_addresses,
        second.server: second.parsed_addresses,
    }
    assert records[first.server] == ["192.0.2.10"]
    assert records[second.server] == ["192.0.2.20"]


@pytest.mark.parametrize("lan", ["192.168.1.10", "10.0.0.10", "172.20.0.10"])
def test_default_route_lan_excludes_rfc1918_bridges(lan: str) -> None:
    candidates = [
        ("172.17.0.1", 16, "docker0"),
        ("192.168.64.1", 24, "bridge100"),
        (lan, 24, "en0"),
    ]
    assert mdns._select_advertised_addresses(candidates, preferred_address=lan) == [lan]


@pytest.mark.parametrize(
    ("candidates", "preferred", "expected"),
    [
        (
            [("172.17.0.1", 16, "docker0"), ("192.168.1.10", 24, "en0")],
            "192.168.1.10",
            ["192.168.1.10"],
        ),
        (
            [("192.168.1.10", 24, "en0"), ("192.168.2.10", 24, "en1")],
            "192.168.2.10",
            ["192.168.2.10", "192.168.1.10"],
        ),
        (
            [("10.8.0.2", 32, "utun3"), ("192.168.1.10", 24, "en0")],
            "10.8.0.2",
            ["192.168.1.10"],
        ),
        (
            [("172.17.0.1", 16, "docker0"), ("10.8.0.2", 32, "utun3")],
            "10.8.0.2",
            ["10.8.0.2", "172.17.0.1"],
        ),
    ],
    ids=[
        "lan-and-docker",
        "ethernet-and-wifi",
        "vpn-default-with-lan",
        "all-excluded-fallback",
    ],
)
def test_interface_filter_preserves_home_lans(candidates, preferred, expected) -> None:
    assert mdns._select_advertised_addresses(candidates, preferred_address=preferred) == expected


@pytest.mark.parametrize(
    "interface",
    [
        "docker0",
        "br-ab123",
        "bridge100",
        "bridge199",
        "vmnet8",
        "vboxnet0",
        "virbr0",
        "vethabcd",
        "utun3",
        "tun0",
        "wg0",
        "tailscale0",
    ],
)
def test_known_virtual_interface_is_excluded(interface: str) -> None:
    assert mdns._select_advertised_addresses(
        [
            ("10.0.0.2", 24, interface),
            ("192.168.1.10", 24, "en0"),
        ]
    ) == ["192.168.1.10"]


@pytest.mark.parametrize("interface", [None, "en0", "eth0", "wlan0", "bridge0", "bridge200"])
def test_unknown_or_lan_interface_is_retained(interface: str | None) -> None:
    assert mdns._select_advertised_addresses(
        [
            ("10.0.0.2", 24, interface),
            ("192.168.1.10", 24, "en0"),
        ]
    ) == ["10.0.0.2", "192.168.1.10"]


def test_unusable_default_route_retains_lan_fallback() -> None:
    assert mdns._select_advertised_addresses(
        [("192.168.1.0", 24, None), ("192.168.1.10", 24, None)],
        preferred_address="192.168.1.0",
    ) == ["192.168.1.10"]


def test_wildcard_socket_duplicates_inherit_interface_filter(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [
            ("172.17.0.1", 16, "docker0"),
            ("192.168.1.10", 24, "en0"),
            ("192.168.2.10", 24, "en1"),
            ("10.8.0.2", 32, "utun3"),
        ],
    )
    monkeypatch.setattr(
        mdns,
        "_iter_socket_ipv4_addresses",
        lambda: [
            ("172.17.0.1", None, None),
            ("10.8.0.2", None, None),
        ],
    )
    monkeypatch.setattr(mdns, "_primary_ipv4_address", lambda: "10.8.0.2")
    advertisement = build_advertisement(host="0.0.0.0", port=8765)
    assert advertisement is not None
    assert advertisement.parsed_addresses == ["192.168.1.10", "192.168.2.10"]


def test_ifaddr_preserves_interface_name(monkeypatch: pytest.MonkeyPatch) -> None:
    from types import SimpleNamespace

    import ifaddr

    monkeypatch.setattr(
        ifaddr,
        "get_adapters",
        lambda: [
            SimpleNamespace(
                name="en0", ips=[SimpleNamespace(ip="192.168.1.10", network_prefix=24)]
            ),
            SimpleNamespace(
                name="docker0",
                ips=[SimpleNamespace(ip="172.17.0.1", network_prefix=16)],
            ),
        ],
    )
    assert mdns._iter_ifaddr_ipv4_addresses() == [
        ("192.168.1.10", 24, "en0"),
        ("172.17.0.1", 16, "docker0"),
    ]


def test_concrete_host_overrides_default_route(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(mdns, "_primary_ipv4_address", lambda: "192.168.1.10")
    advertisement = build_advertisement(host="192.0.2.10", port=8765)
    assert advertisement is not None
    assert advertisement.parsed_addresses == ["192.0.2.10"]


def test_wildcard_host_advertises_all_usable_non_loopback_ipv4(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [
            ("127.0.0.1", 8, None),
            ("192.0.2.10", 24, None),
            ("0.0.0.0", 24, None),
        ],
    )
    monkeypatch.setattr(
        mdns,
        "_iter_socket_ipv4_addresses",
        lambda: [("192.0.2.10", None, None), ("10.0.0.5", None, None)],
    )

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    # 10.0.0.5 is RFC1918 (tier 1) and sorts ahead of the public 192.0.2.10.
    assert advertisement.parsed_addresses == ["10.0.0.5", "192.0.2.10"]


def test_rfc1918_addresses_are_advertised_before_others(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [
            ("203.0.113.7", 24, None),  # public, tier 2
            ("192.168.0.10", 24, None),  # RFC1918, tier 1
            ("10.1.2.3", 8, None),  # RFC1918, tier 1
            ("172.16.5.6", 12, None),  # RFC1918, tier 1
        ],
    )
    monkeypatch.setattr(mdns, "_iter_socket_ipv4_addresses", lambda: [])

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    # All kept; the three private addresses move ahead of the public one, and
    # within each tier the original enumeration order is preserved.
    assert advertisement.parsed_addresses == [
        "192.168.0.10",
        "10.1.2.3",
        "172.16.5.6",
        "203.0.113.7",
    ]


def test_cgnat_address_is_kept_but_ordered_after_rfc1918(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Reproduces the real-device scenario from the issue: a CGNAT address
    # (100.64.0.0/10, here as a /32 host address) must remain advertised but be
    # tried only after the reachable LAN address.
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [
            ("100.64.10.20", 32, None),  # CGNAT, tier 2 (must NOT be dropped)
            ("192.168.0.10", 24, None),  # RFC1918 LAN, tier 1
        ],
    )
    monkeypatch.setattr(mdns, "_iter_socket_ipv4_addresses", lambda: [])

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    assert advertisement.parsed_addresses == ["192.168.0.10", "100.64.10.20"]


def test_network_and_broadcast_addresses_are_excluded(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [
            ("192.168.0.0", 24, None),  # network address -> excluded
            ("192.168.0.255", 24, None),  # broadcast address -> excluded
            ("192.168.0.10", 24, None),  # host address -> kept
        ],
    )
    monkeypatch.setattr(mdns, "_iter_socket_ipv4_addresses", lambda: [])

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    assert advertisement.parsed_addresses == ["192.168.0.10"]


def test_host_prefixes_are_not_treated_as_network_or_broadcast(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # /31 and /32 have no distinct network/broadcast address; a legitimate host
    # IP on such a prefix must not be dropped.
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [("192.168.5.0", 32, None), ("10.0.0.0", 31, None)],
    )
    monkeypatch.setattr(mdns, "_iter_socket_ipv4_addresses", lambda: [])

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    assert advertisement.parsed_addresses == ["192.168.5.0", "10.0.0.0"]


def test_addresses_without_prefix_are_not_excluded(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The socket source carries no prefix; a ".0"-looking address from it cannot
    # be classified as a network address and must be kept (zero-regression).
    monkeypatch.setattr(mdns, "_iter_ifaddr_ipv4_addresses", lambda: [])
    monkeypatch.setattr(
        mdns,
        "_iter_socket_ipv4_addresses",
        lambda: [("192.168.0.0", None, None), ("192.168.0.10", None, None)],
    )

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    assert advertisement.parsed_addresses == ["192.168.0.0", "192.168.0.10"]


def test_socket_source_inherits_ifaddr_prefix_for_network_address_filtering(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Regression: an interface can end up holding its subnet's network address
    # (e.g. ``192.168.1.0/24`` with both subnet and ifaddr-reported IP equal to
    # ``192.168.1.0``) under unusual host routing or bridge configurations.
    # The ifaddr source reports it with the correct prefix; the socket source
    # (via ``getaddrinfo``) returns the same address with no prefix. Without
    # bridging the two, the socket entry would slip past
    # ``_is_network_or_broadcast_address`` and zeroconf would later crash with
    # ``EADDRNOTAVAIL`` trying to ``sendto`` that address. The enumerator must
    # adopt the ifaddr prefix for matching socket addresses so the existing
    # network/broadcast filter applies uniformly.
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [("192.168.1.42", 24, None), ("192.168.1.0", 24, None)],
    )
    monkeypatch.setattr(
        mdns,
        "_iter_socket_ipv4_addresses",
        lambda: [("192.168.1.0", None, None), ("192.168.1.42", None, None)],
    )

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    assert "192.168.1.0" not in advertisement.parsed_addresses
    assert "192.168.1.42" in advertisement.parsed_addresses


def test_mixed_tier_ordering_preserves_within_tier_order(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # ifaddr source (with prefixes) is enumerated before the socket source; the
    # stable sort must keep that combined order within each tier.
    monkeypatch.setattr(
        mdns,
        "_iter_ifaddr_ipv4_addresses",
        lambda: [("198.51.100.4", 24, None), ("192.168.1.2", 24, None)],
    )
    monkeypatch.setattr(
        mdns,
        "_iter_socket_ipv4_addresses",
        lambda: [("203.0.113.9", None, None), ("10.5.5.5", None, None)],
    )

    advertisement = build_advertisement(host="0.0.0.0", port=8765)

    assert advertisement is not None
    # tier 1 (private), original order: 192.168.1.2 then 10.5.5.5;
    # tier 2 (other), original order: 198.51.100.4 then 203.0.113.9.
    assert advertisement.parsed_addresses == [
        "192.168.1.2",
        "10.5.5.5",
        "198.51.100.4",
        "203.0.113.9",
    ]


@pytest.mark.parametrize("host", ["127.0.0.1", "localhost", "::1"])
def test_loopback_hosts_are_not_advertised(host: str) -> None:
    assert build_advertisement(host=host, port=8765) is None


@pytest.mark.parametrize("port", [0, -1, 65536])
def test_unpublishable_ports_are_not_advertised(port: int) -> None:
    assert build_advertisement(host="192.0.2.10", port=port) is None


@pytest.mark.asyncio
async def test_advertiser_registers_service(monkeypatch: pytest.MonkeyPatch) -> None:
    instances: list[FakeAsyncZeroconf] = []

    class FakeServiceInfo:
        def __init__(self, service_type: str, service_name: str, **kwargs) -> None:
            self.type = service_type
            self.name = service_name
            self.kwargs = kwargs

    class FakeAsyncZeroconf:
        def __init__(self, *, interfaces=None) -> None:
            self.interfaces = interfaces
            self.registered = []
            self.unregistered = []
            self.closed = False
            instances.append(self)

        async def async_register_service(
            self, info: FakeServiceInfo, *, allow_name_change: bool = False
        ) -> None:
            self.registered.append((info, allow_name_change))

        async def async_unregister_service(self, info: FakeServiceInfo) -> None:
            self.unregistered.append(info)

        async def async_close(self) -> None:
            self.closed = True

    monkeypatch.setattr(mdns, "_load_zeroconf_classes", lambda: (FakeAsyncZeroconf, FakeServiceInfo))
    monkeypatch.setattr(mdns, "_enumerate_usable_ipv4_addresses", lambda: ["192.0.2.10", "10.0.0.5"])

    advertiser = MdnsAdvertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")

    assert len(instances) == 1
    zeroconf = instances[0]
    assert zeroconf.interfaces == ["192.0.2.10", "10.0.0.5"]
    assert len(zeroconf.registered) == 1
    info, allow_name_change = zeroconf.registered[0]
    assert allow_name_change is True
    assert info.type == "_stackchan-mcp._tcp.local."
    assert info.name == "stackchan-mcp._stackchan-mcp._tcp.local."
    assert info.kwargs["port"] == 8765
    assert info.kwargs["properties"] == {"path": "/", "version": "1"}
    assert info.kwargs["parsed_addresses"] == ["192.0.2.10", "10.0.0.5"]

    await advertiser.stop()

    assert zeroconf.unregistered == [info]
    assert zeroconf.closed is True


@pytest.mark.asyncio
async def test_advertiser_warns_when_service_name_changes(
    monkeypatch: pytest.MonkeyPatch,
    caplog: pytest.LogCaptureFixture,
) -> None:
    instances: list[RenamingAsyncZeroconf] = []
    renamed_service_name = "stackchan-mcp-2._stackchan-mcp._tcp.local."

    class FakeServiceInfo:
        def __init__(self, service_type: str, service_name: str, **kwargs) -> None:
            self.type = service_type
            self.name = service_name
            self.kwargs = kwargs

    class RenamingAsyncZeroconf:
        def __init__(self, *, interfaces=None) -> None:
            self.interfaces = interfaces
            self.registered = []
            self.unregistered = []
            self.closed = False
            instances.append(self)

        async def async_register_service(
            self, info: FakeServiceInfo, *, allow_name_change: bool = False
        ) -> None:
            self.registered.append((info, allow_name_change))
            info.name = renamed_service_name

        async def async_unregister_service(self, info: FakeServiceInfo) -> None:
            self.unregistered.append(info)

        async def async_close(self) -> None:
            self.closed = True

    monkeypatch.setattr(
        mdns,
        "_load_zeroconf_classes",
        lambda: (RenamingAsyncZeroconf, FakeServiceInfo),
    )
    caplog.set_level("WARNING", logger=mdns.__name__)

    advertiser = MdnsAdvertiser()
    await advertiser.start(host="192.0.2.10", port=8765, path="/")

    assert len(instances) == 1
    zeroconf = instances[0]
    assert len(zeroconf.registered) == 1
    info, allow_name_change = zeroconf.registered[0]
    assert allow_name_change is True
    assert info.name == renamed_service_name
    assert zeroconf.closed is False
    assert advertiser._zeroconf is zeroconf
    assert advertiser._service_info is info
    assert "modified name" in caplog.text
    assert renamed_service_name in caplog.text

    await advertiser.stop()

    assert zeroconf.unregistered == [info]
    assert zeroconf.closed is True


@pytest.mark.asyncio
async def test_advertiser_closes_zeroconf_when_registration_fails(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    instances: list[FailingAsyncZeroconf] = []

    class FakeServiceInfo:
        def __init__(self, service_type: str, service_name: str, **kwargs) -> None:
            self.type = service_type
            self.name = service_name
            self.kwargs = kwargs

    class FailingAsyncZeroconf:
        def __init__(self, *, interfaces=None) -> None:
            self.interfaces = interfaces
            self.closed = False
            instances.append(self)

        async def async_register_service(
            self, info: FakeServiceInfo, *, allow_name_change: bool = False
        ) -> None:
            raise RuntimeError("mock registration failure")

        async def async_close(self) -> None:
            self.closed = True

    monkeypatch.setattr(
        mdns,
        "_load_zeroconf_classes",
        lambda: (FailingAsyncZeroconf, FakeServiceInfo),
    )

    advertiser = MdnsAdvertiser()
    with pytest.raises(RuntimeError, match="mock registration failure"):
        await advertiser.start(host="192.0.2.10", port=8765, path="/")

    assert len(instances) == 1
    assert instances[0].closed is True


class RecordingServiceInfo:
    def __init__(self, service_type: str, service_name: str, **kwargs) -> None:
        self.type = service_type
        self.name = service_name
        self.kwargs = kwargs


class RecordingAsyncZeroconf:
    instances: list[RecordingAsyncZeroconf] = []
    register_errors: list[Exception | None] = []

    @classmethod
    def reset(cls, register_errors: list[Exception | None] | None = None) -> None:
        cls.instances = []
        cls.register_errors = list(register_errors or [])

    def __init__(self, *, interfaces=None) -> None:
        self.interfaces = interfaces
        self.registered = []
        self.register_attempts = []
        self.unregistered = []
        self.closed = False
        self.close_count = 0
        self.instances.append(self)

    async def async_register_service(
        self, info: RecordingServiceInfo, *, allow_name_change: bool = False
    ) -> None:
        self.register_attempts.append((info, allow_name_change))
        if self.register_errors:
            error = self.register_errors.pop(0)
            if error is not None:
                raise error
        self.registered.append((info, allow_name_change))

    async def async_unregister_service(self, info: RecordingServiceInfo) -> None:
        self.unregistered.append(info)

    async def async_close(self) -> None:
        self.closed = True
        self.close_count += 1


def install_recording_zeroconf(
    monkeypatch: pytest.MonkeyPatch,
    *,
    register_errors: list[Exception | None] | None = None,
) -> list[RecordingAsyncZeroconf]:
    RecordingAsyncZeroconf.reset(register_errors)
    monkeypatch.setattr(
        mdns,
        "_load_zeroconf_classes",
        lambda: (RecordingAsyncZeroconf, RecordingServiceInfo),
    )
    return RecordingAsyncZeroconf.instances


def fast_advertiser(interval: float = 0.01) -> MdnsAdvertiser:
    # Production intervals are validated at 10-300 seconds; tests shorten the
    # private sleep value after construction so debounce behavior is practical.
    advertiser = MdnsAdvertiser(refresh_interval=10.0)
    advertiser._refresh_interval = interval
    return advertiser


def sequence_addresses(values: list[list[str]]):
    remaining = [list(value) for value in values]
    fallback = list(remaining[-1])

    def next_addresses() -> list[str]:
        if remaining:
            return list(remaining.pop(0))
        return list(fallback)

    return next_addresses


async def wait_until(predicate, *, timeout: float = 1.0) -> None:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while loop.time() < deadline:
        if predicate():
            return
        await asyncio.sleep(0.005)
    assert predicate()


@pytest.mark.asyncio
async def test_refresh_stable_address_list_does_not_reconfigure(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        lambda: ["198.51.100.10"],
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")
    await asyncio.sleep(advertiser._refresh_interval * 6)

    assert len(instances) == 1
    assert instances[0].unregistered == []
    assert len(instances[0].registered) == 1

    await advertiser.stop()


@pytest.mark.asyncio
async def test_refresh_changed_address_list_reconfigures_once_after_debounce(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    old = ["198.51.100.10"]
    new = ["203.0.113.20"]
    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([old, old, new, new, new]),
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")
    await wait_until(lambda: len(instances) == 2)

    assert instances[0].unregistered == [instances[0].registered[0][0]]
    assert instances[0].closed is True
    assert len(instances[1].registered) == 1
    assert advertiser._last_advertised_addresses == tuple(new)

    await advertiser.stop()


@pytest.mark.asyncio
async def test_refresh_transient_single_tick_change_does_not_reconfigure(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    old = ["198.51.100.10"]
    transient = ["203.0.113.20"]
    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([old, transient, old, old, old, old]),
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")
    await asyncio.sleep(advertiser._refresh_interval * 6)

    assert len(instances) == 1
    assert instances[0].unregistered == []
    assert advertiser._last_advertised_addresses == tuple(old)

    await advertiser.stop()


@pytest.mark.asyncio
async def test_refresh_survives_transient_empty_build_result(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    old = ["198.51.100.10"]
    new = ["203.0.113.20"]
    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([old, new, new, new, new, new]),
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")

    original_build_advertisement = mdns.build_advertisement
    empty_builds = [None]

    def flaky_build_advertisement(*, host: str, port: int, path: str = "/"):
        if empty_builds:
            return empty_builds.pop(0)
        return original_build_advertisement(host=host, port=port, path=path)

    monkeypatch.setattr(mdns, "build_advertisement", flaky_build_advertisement)
    await wait_until(lambda: len(instances) == 2)

    assert advertiser._refresh_task is not None
    assert not advertiser._refresh_task.done()
    assert instances[0].unregistered == [instances[0].registered[0][0]]
    assert advertiser._last_advertised_addresses == tuple(new)

    await advertiser.stop()


@pytest.mark.asyncio
async def test_double_start_closes_previous_registration_before_new_start(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    instances = install_recording_zeroconf(monkeypatch)

    advertiser = fast_advertiser()
    await advertiser.start(host="198.51.100.10", port=8765, path="/")
    first_task = advertiser._refresh_task
    await advertiser.start(host="203.0.113.20", port=8765, path="/")

    assert first_task is not None
    assert first_task.done()
    assert len(instances) == 2
    assert instances[0].unregistered == [instances[0].registered[0][0]]
    assert instances[0].closed is True
    assert instances[1].closed is False
    assert advertiser._zeroconf is instances[1]

    await advertiser.stop()


@pytest.mark.asyncio
async def test_stop_cancels_active_refresh_task_and_closes_registration(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    instances = install_recording_zeroconf(monkeypatch)

    advertiser = fast_advertiser(interval=0.05)
    await advertiser.start(host="198.51.100.10", port=8765, path="/")
    refresh_task = advertiser._refresh_task
    await advertiser.stop()

    assert refresh_task is not None
    assert refresh_task.done()
    assert instances[0].unregistered == [instances[0].registered[0][0]]
    assert instances[0].closed is True
    assert advertiser._refresh_task is None
    assert advertiser._zeroconf is None
    assert advertiser._service_info is None


@pytest.mark.asyncio
async def test_refresh_compares_canonical_address_order(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    canonical = ["198.51.100.10", "203.0.113.20"]
    forced_order = ["203.0.113.20", "198.51.100.10"]

    # The refresh loop compares the canonical list returned by the enumerator.
    # Raw interface ordering is normalized by _select_advertised_addresses; if
    # the canonical order is unchanged, no recycle is needed.
    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([canonical, canonical, canonical, canonical]),
    )
    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")
    await asyncio.sleep(advertiser._refresh_interval * 3)
    assert len(instances) == 1
    assert instances[0].unregistered == []
    await advertiser.stop()

    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([canonical, forced_order, forced_order, forced_order]),
    )
    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")
    await wait_until(lambda: len(instances) == 2)
    assert instances[0].unregistered == [instances[0].registered[0][0]]
    assert advertiser._last_advertised_addresses == tuple(forced_order)
    await advertiser.stop()


@pytest.mark.asyncio
async def test_refresh_register_failure_cleans_up_and_loop_continues(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    old = ["198.51.100.10"]
    new = ["203.0.113.20"]
    instances = install_recording_zeroconf(
        monkeypatch,
        register_errors=[None, RuntimeError("mock refresh registration failure"), None],
    )
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([old, new, new, new, new, new]),
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")
    await wait_until(lambda: len(instances) == 3 and advertiser._zeroconf is instances[2])

    assert instances[0].unregistered == [instances[0].registered[0][0]]
    assert instances[0].closed is True
    assert instances[1].registered == []
    assert instances[1].closed is True
    assert len(instances[2].registered) == 1
    assert advertiser._refresh_task is not None
    assert not advertiser._refresh_task.done()
    assert advertiser._last_advertised_addresses == tuple(new)

    await advertiser.stop()


def test_refresh_interval_validation() -> None:
    with pytest.raises(ValueError):
        MdnsAdvertiser(refresh_interval=5)
    with pytest.raises(ValueError):
        MdnsAdvertiser(refresh_interval=500)
    assert MdnsAdvertiser(refresh_interval=30)._refresh_interval == 30


@pytest.mark.asyncio
async def test_stop_is_idempotent(monkeypatch: pytest.MonkeyPatch) -> None:
    instances = install_recording_zeroconf(monkeypatch)

    advertiser = fast_advertiser()
    await advertiser.start(host="198.51.100.10", port=8765, path="/")
    await advertiser.stop()
    close_count = instances[0].close_count
    unregister_count = len(instances[0].unregistered)

    await advertiser.stop()

    assert instances[0].close_count == close_count
    assert len(instances[0].unregistered) == unregister_count


@pytest.mark.asyncio
async def test_refresh_with_concrete_host_ignores_unrelated_interface_changes(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Codex review round 2 Finding 1: when started with a concrete HOST
    (not a wildcard), the refresh loop must compare against that HOST's
    resolution only — not against the full host-interface enumeration.
    Otherwise a multi-NIC / Tailscale host churns the registration on every
    refresh tick even though the actually-advertised set never changes.
    """
    instances = install_recording_zeroconf(monkeypatch)

    # The concrete-host resolver stays constant across all refresh ticks.
    monkeypatch.setattr(
        mdns,
        "_resolve_concrete_host_ipv4_addresses",
        lambda host: ["192.0.2.10"] if host == "192.0.2.10" else [],
    )
    # The wildcard enumerator returns a DIFFERENT (extra) set that must NOT
    # influence the refresh decision when host is concrete.
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        lambda: ["192.0.2.10", "10.0.0.5"],
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="192.0.2.10", port=8765, path="/")

    # Let several refresh cycles run; the concrete-host comparison must stay
    # stable so no second zeroconf instance is ever created.
    await asyncio.sleep(advertiser._refresh_interval * 3)

    assert len(instances) == 1
    assert instances[0].unregistered == []
    assert advertiser._last_advertised_addresses == ("192.0.2.10",)

    await advertiser.stop()


@pytest.mark.asyncio
async def test_reconfigure_register_failure_then_ip_revert_still_recovers(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Codex review round 2 Finding 2: if a reconfigure closes the old
    registration but the new registration then fails, the cached
    ``_last_advertised_addresses`` must not point at the now-defunct old
    value. Otherwise, if the host IP reverts to the old value (Wi-Fi
    re-association, DHCP renewal returning the previous lease, etc.), the
    refresh loop sees ``current == _last_advertised`` and stays quiet —
    leaving the advertisement permanently dead until manual restart.
    """
    old = ["198.51.100.10"]
    new = ["203.0.113.20"]

    # Sequence: initial register (old) → refresh observes new → debounce
    # confirm new → reconfigure: close old, register new fails → IP reverts
    # to old → refresh observes old (≠ None after the failed reconfigure)
    # → debounce confirm old → reconfigure: register succeeds.
    instances = install_recording_zeroconf(
        monkeypatch,
        register_errors=[None, RuntimeError("mock reconfigure register fail"), None],
    )
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([old, new, new, old, old, old, old]),
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")

    # Wait until the third zeroconf instance is created — this is the recovery
    # that only happens if the previous failure cleared _last_advertised.
    await wait_until(
        lambda: len(instances) == 3 and advertiser._zeroconf is instances[2]
    )

    # instance 0: original (old IP) was closed during the failed reconfigure.
    assert instances[0].closed is True
    # instance 1: register call raised, no successful registration recorded,
    # internal cleanup closes the partially-constructed zeroconf.
    assert instances[1].registered == []
    assert instances[1].closed is True
    # instance 2: recovery succeeded against the reverted (old) IP.
    assert len(instances[2].registered) == 1
    assert advertiser._last_advertised_addresses == tuple(old)
    assert advertiser._refresh_task is not None
    assert not advertiser._refresh_task.done()

    await advertiser.stop()


@pytest.mark.asyncio
async def test_reconfigure_close_failure_then_revert_to_old_ip_still_recovers(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Codex review round 2 Finding 3: if the OLD-instance close itself
    raises during reconfigure (e.g. the old interface vanished and
    ``async_unregister`` / ``async_close`` fail), the cached
    ``_last_advertised_addresses`` must already have been cleared so that
    a subsequent IP revert is still picked up by the refresh loop. If we
    cleared only after a successful close, this code path would leave the
    cache pointing at the old (now-dead) registration and silently stop
    advertising forever.
    """
    old = ["198.51.100.10"]
    new = ["203.0.113.20"]

    # Two healthy registrations bracket the failing reconfigure attempt.
    # instance 0 is the initial register; instance 1 is the recovery
    # register after the IP reverts.
    instances = install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        sequence_addresses([old, new, new, old, old, old, old]),
    )

    # Make the close path fail exactly once — on the FIRST close that
    # follows the initial register. Subsequent closes (on the recovery
    # path and stop()) succeed normally.
    close_calls = {"n": 0}
    original_close = RecordingAsyncZeroconf.async_close

    async def flaky_async_close(self: RecordingAsyncZeroconf) -> None:
        close_calls["n"] += 1
        if close_calls["n"] == 1:
            self.closed = True
            self.close_count += 1
            raise RuntimeError("mock async_close failure on old-interface teardown")
        await original_close(self)

    monkeypatch.setattr(
        RecordingAsyncZeroconf, "async_close", flaky_async_close
    )

    advertiser = fast_advertiser()
    await advertiser.start(host="0.0.0.0", port=8765, path="/")

    # Wait for the recovery: a second zeroconf instance is only created
    # if the refresh loop saw _last_advertised_addresses == None after
    # the failed close (rather than the stale old value) and re-tried.
    await wait_until(
        lambda: len(instances) == 2 and advertiser._zeroconf is instances[1]
    )

    # instance 0: marked closed (the mock still set the flag) and the
    # failure was raised so reconfigure aborted partway through.
    assert instances[0].closed is True
    # instance 1: recovery succeeded against the reverted (old) IP.
    assert len(instances[1].registered) == 1
    assert advertiser._last_advertised_addresses == tuple(old)
    assert advertiser._refresh_task is not None
    assert not advertiser._refresh_task.done()

    await advertiser.stop()


@pytest.mark.asyncio
async def test_register_advertisement_cancellation_closes_zeroconf(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Codex review round 2 Finding 4: an ``asyncio.CancelledError`` arriving
    mid-``async_register_service`` (e.g. external ``stop()`` races the
    initial register, or a ``double-start()`` cancels the previous in-flight
    register) must still close the partially-constructed ``AsyncZeroconf``.
    Otherwise the multicast sockets and any partial registration leak past
    the cancelled task. ``except Exception`` is not sufficient because
    ``CancelledError`` derives from ``BaseException`` in Python 3.8+.
    """
    install_recording_zeroconf(monkeypatch)
    monkeypatch.setattr(
        mdns,
        "_enumerate_usable_ipv4_addresses",
        lambda: ["192.0.2.10"],
    )

    # Make register raise CancelledError; capture the zeroconf instance
    # whose async_close should still be called.
    captured: dict[str, RecordingAsyncZeroconf | None] = {"zc": None}

    async def cancelling_register(
        self: RecordingAsyncZeroconf,
        info: RecordingServiceInfo,
        *,
        allow_name_change: bool = False,
    ) -> None:
        captured["zc"] = self
        raise asyncio.CancelledError("mock cancellation during register")

    monkeypatch.setattr(
        RecordingAsyncZeroconf, "async_register_service", cancelling_register
    )

    advertiser = fast_advertiser()
    with pytest.raises(asyncio.CancelledError):
        await advertiser.start(host="0.0.0.0", port=8765, path="/")

    # The partially-constructed AsyncZeroconf must have been closed
    # even though the failure was a BaseException-derived CancelledError.
    assert captured["zc"] is not None
    assert captured["zc"].closed is True
