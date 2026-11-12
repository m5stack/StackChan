import asyncio
import json
import math
from typing import Any

import pytest
import pytest_asyncio

from stackchan_mcp import follow_pose_stream as fps
from stackchan_mcp.follow_pose_stream import (
    FollowPoseStream,
    FollowPoseStreamConfig,
    map_sensor_to_servo,
    step_clamp,
)


_URL_BASE = "ws://" + "example.invalid"


def _url(path: str = "pose") -> str:
    return f"{_URL_BASE}/{path}"


_GET_HEAD_ANGLES = "self.robot.get_head_angles"
_SET_HEAD_ANGLES = "self.robot.set_head_angles"
_WIFI_SET_POWER_SAVE = "self.wifi.set_power_save"


def _wrap_call_payload(
    payload: dict[str, Any],
    *,
    is_error: bool = False,
) -> dict[str, Any]:
    return {
        "content": [{"text": json.dumps(payload)}],
        "isError": is_error,
    }


def _wrap_get_head_angles(
    yaw: Any,
    pitch: Any,
    *,
    is_error: bool = False,
) -> dict[str, Any]:
    return _wrap_call_payload({"yaw": yaw, "pitch": pitch}, is_error=is_error)


class _FakeESP32:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self._replies: dict[str, tuple[Any, Any]] = {}
        self._exc: dict[str, BaseException] = {}

    def set_reply(self, method: str, result: Any, error: Any = None) -> None:
        self._replies[method] = (result, error)
        self._exc.pop(method, None)

    def set_raise(self, method: str, exc: BaseException) -> None:
        self._exc[method] = exc

    async def call_tool(
        self,
        method: str,
        args: dict[str, Any],
    ) -> tuple[Any, Any]:
        self.calls.append((method, args))
        if method in self._exc:
            raise self._exc[method]
        if method in self._replies:
            return self._replies[method]
        return {"ok": True}, None


class _FakeGateway:
    def __init__(self) -> None:
        self.esp32 = _FakeESP32()


class _FakeWebSocket:
    def __init__(
        self,
        messages: list[str],
        *,
        clock: "_Clock | None" = None,
        tick_s: float = 0.0,
    ) -> None:
        self._messages = messages
        self._clock = clock
        self._tick_s = tick_s
        self._index = 0

    def __aiter__(self) -> "_FakeWebSocket":
        return self

    async def __anext__(self) -> str:
        if self._index >= len(self._messages):
            raise StopAsyncIteration
        message = self._messages[self._index]
        self._index += 1
        if self._clock is not None:
            self._clock.advance(self._tick_s)
        return message


class _Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def time(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


def _mark_seeded(follower: FollowPoseStream) -> None:
    follower._initial_pose_seeded = True


class _RefusingConnect:
    async def __aenter__(self) -> None:
        raise ConnectionRefusedError("refused")

    async def __aexit__(self, *exc_info: object) -> bool:
        return False


def _refusing_connect(_url: str) -> _RefusingConnect:
    return _RefusingConnect()


@pytest_asyncio.fixture(autouse=True)
async def _reset_singleton() -> Any:
    await fps.stop_follow()
    yield
    await fps.stop_follow()


def test_map_sensor_to_servo_1to1_within_range() -> None:
    assert map_sensor_to_servo(
        30,
        -10,
        flip_yaw=1,
        flip_pitch=1,
        pitch_center_deg=45,
    ) == (30, 35)


def test_map_sensor_to_servo_saturates_at_clamp() -> None:
    assert map_sensor_to_servo(
        200,
        200,
        flip_yaw=1,
        flip_pitch=1,
        pitch_center_deg=45,
    ) == (90, 85)


def test_map_sensor_to_servo_negative_saturates() -> None:
    assert map_sensor_to_servo(
        -200,
        -200,
        flip_yaw=1,
        flip_pitch=1,
        pitch_center_deg=45,
    ) == (-90, 5)


def test_map_sensor_to_servo_flip_inverts() -> None:
    assert map_sensor_to_servo(
        30,
        0,
        flip_yaw=-1,
        flip_pitch=1,
        pitch_center_deg=45,
    )[0] == -30


def test_step_clamp_within_step_passes() -> None:
    assert step_clamp(target=10, last=8, max_step_deg=5) == 10


def test_step_clamp_above_step_limits() -> None:
    assert step_clamp(target=20, last=0, max_step_deg=5) == 5


def test_step_clamp_below_step_limits() -> None:
    assert step_clamp(target=-20, last=0, max_step_deg=5) == -5



@pytest.mark.asyncio
async def test_seed_from_device_initializes_last_servo() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(60, 70))
    follower = FollowPoseStream(gateway, FollowPoseStreamConfig(url=_url("seed-ok")))

    await follower._seed_from_device()

    assert follower._last_servo_yaw == 60
    assert follower._last_servo_pitch == 70
    assert follower._initial_pose_seeded is True


@pytest.mark.asyncio
async def test_seed_from_device_failure_does_not_lock_seeded_flag() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _GET_HEAD_ANGLES,
        _wrap_get_head_angles(60, 70, is_error=True),
    )
    follower = FollowPoseStream(
        gateway,
        FollowPoseStreamConfig(url=_url("seed-error")),
    )

    await follower._seed_from_device()

    assert follower._last_servo_yaw == 0
    assert follower._last_servo_pitch == 45
    assert follower._last_error == "self.robot.get_head_angles returned isError"
    assert follower._initial_pose_seeded is False

    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(30, 55))
    await follower._seed_from_device()

    assert follower._last_servo_yaw == 30
    assert follower._last_servo_pitch == 55
    assert follower._initial_pose_seeded is True


@pytest.mark.asyncio
async def test_seed_from_device_exception_does_not_lock_seeded_flag() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_raise(_GET_HEAD_ANGLES, RuntimeError("seed failed"))
    follower = FollowPoseStream(
        gateway,
        FollowPoseStreamConfig(url=_url("seed-exception")),
    )

    await follower._seed_from_device()

    assert follower._last_servo_yaw == 0
    assert follower._last_servo_pitch == 45
    assert follower._last_error == "seed failed"
    assert follower._initial_pose_seeded is False

    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(-20, 65))
    await follower._seed_from_device()

    assert follower._last_servo_yaw == -20
    assert follower._last_servo_pitch == 65
    assert follower._initial_pose_seeded is True


@pytest.mark.asyncio
async def test_seed_from_device_handles_plain_dict_payload() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, {"yaw": 60, "pitch": 70})
    follower = FollowPoseStream(
        gateway,
        FollowPoseStreamConfig(url=_url("seed-plain-dict")),
    )

    await follower._seed_from_device()

    assert follower._last_servo_yaw == 60
    assert follower._last_servo_pitch == 70
    assert follower._initial_pose_seeded is True


@pytest.mark.asyncio
async def test_seed_from_device_unpack_failure_does_not_lock_seeded_flag() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _GET_HEAD_ANGLES,
        {"content": [{"text": "not json"}], "isError": False},
    )
    follower = FollowPoseStream(
        gateway,
        FollowPoseStreamConfig(url=_url("seed-malformed")),
    )

    await follower._seed_from_device()

    assert follower._last_servo_yaw == 0
    assert follower._last_servo_pitch == 45
    assert follower._initial_pose_seeded is False

    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(10, 75))
    await follower._seed_from_device()

    assert follower._last_servo_yaw == 10
    assert follower._last_servo_pitch == 75
    assert follower._initial_pose_seeded is True


@pytest.mark.asyncio
async def test_consume_step_clamps_from_seeded_position() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(90, 45))
    cfg = FollowPoseStreamConfig(
        url=_url("seeded-step-clamp"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)

    await follower._seed_from_device()
    gateway.esp32.calls.clear()
    await follower._consume(_FakeWebSocket([json.dumps({"yaw": -90, "pitch": 0})]))

    assert gateway.esp32.calls == [
        (
            _SET_HEAD_ANGLES,
            {"yaw": 85, "pitch": 45, "speed_dps": 240},
        )
    ]


@pytest.mark.asyncio
async def test_consume_reseeds_when_initial_seed_failed_then_advances() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(80, 20))
    cfg = FollowPoseStreamConfig(
        url=_url("consume-reseed-success"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    follower._initial_pose_seeded = False
    follower._last_servo_yaw = 0
    follower._last_servo_pitch = 45
    # This test isolates the seed-retry path; mark WiFi PS apply as
    # already-successful so the F3 reapply gate does not fire here.
    follower._wifi_ps_apply_result = {
        "ok": True,
        "previous": "min_modem",
        "current": "none",
    }

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 0, "pitch": 0})]))

    assert follower._initial_pose_seeded is True
    assert follower._last_servo_yaw == 75
    assert follower._last_servo_pitch == 25
    assert abs(follower._last_servo_yaw - 80) <= cfg.max_step_deg
    assert abs(follower._last_servo_pitch - 20) <= cfg.max_step_deg
    assert gateway.esp32.calls == [
        (_GET_HEAD_ANGLES, {}),
        (
            _SET_HEAD_ANGLES,
            {"yaw": 75, "pitch": 25, "speed_dps": 240},
        ),
    ]


@pytest.mark.asyncio
async def test_consume_reseeded_then_reapplies_wifi_ps_when_start_failed() -> None:
    """F3 fix: if WiFi PS apply failed at start (ESP32 disconnected),
    the in-stream seed retry path must also retry the WiFi PS apply so
    the DTIM jitter this tool exists to avoid does not persist.
    """
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(80, 20))
    gateway.esp32.set_reply(
        _WIFI_SET_POWER_SAVE,
        {"ok": True, "previous": "min_modem", "current": "none"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("consume-reseed-wifi-ps-reapply"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    follower._initial_pose_seeded = False
    follower._last_servo_yaw = 0
    follower._last_servo_pitch = 45
    # Simulate the start-time WiFi PS apply having failed because the
    # device was not yet connected when start() ran.
    follower._wifi_ps_apply_result = {
        "ok": False,
        "error": "No ESP32 device connected",
    }

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 0, "pitch": 0})]))

    # WiFi PS reapply happened (= the in-stream retry ran).
    assert (_WIFI_SET_POWER_SAVE, {"mode": "none"}) in gateway.esp32.calls
    # And the apply_result now reports ok=True.
    assert isinstance(follower._wifi_ps_apply_result, dict)
    assert follower._wifi_ps_apply_result.get("ok") is True
    # previous was captured so stop() can restore it.
    assert follower._wifi_ps_previous == "min_modem"


@pytest.mark.asyncio
async def test_consume_does_not_reapply_wifi_ps_when_start_succeeded() -> None:
    """Inverse of the F3 fix: when start-time apply succeeded, the
    in-stream seed path must NOT issue a second WiFi PS apply (to
    avoid unnecessary churn on the device's WiFi modem state).
    """
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(80, 20))
    cfg = FollowPoseStreamConfig(
        url=_url("consume-reseed-no-wifi-ps-reapply"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    follower._initial_pose_seeded = False
    follower._last_servo_yaw = 0
    follower._last_servo_pitch = 45
    # Pretend start-time apply succeeded.
    follower._wifi_ps_apply_result = {
        "ok": True,
        "previous": "min_modem",
        "current": "none",
    }
    follower._wifi_ps_previous = "min_modem"

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 0, "pitch": 0})]))

    # No WiFi PS apply call was issued during _consume.
    assert all(call[0] != _WIFI_SET_POWER_SAVE for call in gateway.esp32.calls)


@pytest.mark.asyncio
async def test_run_seed_success_reapplies_wifi_ps_when_start_failed() -> None:
    """F4 fix: when start-time WiFi PS apply failed because the device
    was disconnected at start, but the upstream WS connect path
    (= _run inner loop) ends up succeeding at seed-from-device because
    by then the device is reachable, the WiFi PS apply must also be
    retried on that initial-seed-success path. Otherwise _consume()'s
    in-stream gate is skipped (_initial_pose_seeded is now True) and
    the DTIM jitter persists for the rest of the stream.
    """
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_GET_HEAD_ANGLES, _wrap_get_head_angles(40, 30))
    gateway.esp32.set_reply(
        _WIFI_SET_POWER_SAVE,
        {"ok": True, "previous": "max_modem", "current": "none"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("run-seed-wifi-ps-reapply"),
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    # Simulate the start-time apply having failed.
    follower._wifi_ps_apply_result = {
        "ok": False,
        "error": "No ESP32 device connected",
    }
    # Manually drive the seed path the way _run() does after a fresh
    # upstream WS connect.
    seeded = await follower._seed_from_device()
    assert seeded is True
    await follower._maybe_reapply_wifi_ps()

    # WiFi PS reapply was issued.
    assert (_WIFI_SET_POWER_SAVE, {"mode": "none"}) in gateway.esp32.calls
    assert isinstance(follower._wifi_ps_apply_result, dict)
    assert follower._wifi_ps_apply_result.get("ok") is True
    assert follower._wifi_ps_previous == "max_modem"


@pytest.mark.asyncio
async def test_maybe_reapply_wifi_ps_noop_when_start_succeeded() -> None:
    """Inverse of F4: when start-time apply was already ok, the helper
    must not issue a second WiFi PS apply.
    """
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(url=_url("reapply-noop"))
    follower = FollowPoseStream(gateway, cfg)
    follower._wifi_ps_apply_result = {
        "ok": True,
        "previous": "min_modem",
        "current": "none",
    }
    follower._wifi_ps_previous = "min_modem"

    await follower._maybe_reapply_wifi_ps()

    assert all(call[0] != _WIFI_SET_POWER_SAVE for call in gateway.esp32.calls)
    # apply_result and previous unchanged.
    assert follower._wifi_ps_apply_result.get("ok") is True
    assert follower._wifi_ps_previous == "min_modem"


@pytest.mark.asyncio
async def test_consume_invalidates_device_state_on_disconnect_error() -> None:
    """F6 fix: when set_head_angles fails with a device-disconnect
    error, the cached seed flag and WiFi PS apply state must be
    invalidated so the next reachable frame re-seeds from live head
    pose. Otherwise the gateway keeps `_last_servo_*` from before the
    disconnect and could swing the head on the first command after a
    reboot.
    """
    gateway = _FakeGateway()
    # Initial set_head_angles fails with disconnect; nothing else is
    # exercised since _consume continues to the next frame.
    gateway.esp32.set_reply(
        _SET_HEAD_ANGLES,
        None,
        {"code": -32000, "message": "ESP32 not connected"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("consume-invalidate-on-disconnect"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    # Pretend a previous frame succeeded and cached state.
    follower._initial_pose_seeded = True
    follower._last_servo_yaw = 50
    follower._last_servo_pitch = 30
    follower._wifi_ps_apply_result = {
        "ok": True,
        "previous": "min_modem",
        "current": "none",
    }
    follower._wifi_ps_previous = "min_modem"

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    # Seed invalidated so next frame re-seeds.
    assert follower._initial_pose_seeded is False
    # WiFi PS apply marked failed so _maybe_reapply_wifi_ps re-runs.
    assert isinstance(follower._wifi_ps_apply_result, dict)
    assert follower._wifi_ps_apply_result.get("ok") is False


@pytest.mark.asyncio
async def test_consume_does_not_invalidate_on_servo_bus_error() -> None:
    """Inverse of F6: a non-disconnect error (e.g. servo bus failure)
    must NOT invalidate the cached seed; only true device-disconnect
    errors should reset state. Otherwise transient bus errors would
    constantly force re-seeds and noisy WiFi PS reapplies.
    """
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _SET_HEAD_ANGLES,
        None,
        {"code": -32000, "message": "servo bus write timed out"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("consume-no-invalidate-on-bus-error"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    follower._initial_pose_seeded = True
    follower._last_servo_yaw = 50
    follower._last_servo_pitch = 30
    follower._wifi_ps_apply_result = {
        "ok": True,
        "previous": "min_modem",
        "current": "none",
    }

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    # Seed state preserved (= servo bus errors are transient, not a
    # reboot signal).
    assert follower._initial_pose_seeded is True
    assert follower._wifi_ps_apply_result.get("ok") is True


@pytest.mark.asyncio
async def test_consume_drops_frame_when_reseed_fails() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _GET_HEAD_ANGLES,
        None,
        {"code": -32000, "message": "device disconnected"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("consume-reseed-failure"),
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    follower._initial_pose_seeded = False

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    assert follower._initial_pose_seeded is False
    assert all(call[0] != _SET_HEAD_ANGLES for call in gateway.esp32.calls)
    assert follower._commands_sent == 0
    assert follower._last_sent_at is None
    assert len(follower._samples) == 0


@pytest.mark.asyncio
async def test_run_does_not_seed_when_disabled(monkeypatch: pytest.MonkeyPatch) -> None:
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("no-seed"),
        seed_from_device=False,
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )
    follower = FollowPoseStream(gateway, cfg)

    class _StopWebSocket:
        def __aiter__(self) -> "_StopWebSocket":
            return self

        async def __anext__(self) -> str:
            follower._stop_event.set()
            raise StopAsyncIteration

    class _Connect:
        async def __aenter__(self) -> _StopWebSocket:
            return _StopWebSocket()

        async def __aexit__(self, *exc_info: object) -> bool:
            return False

    def connect(url: str) -> _Connect:
        assert url == cfg.url
        return _Connect()

    monkeypatch.setattr(fps.websockets, "connect", connect)

    await follower._run()

    assert all(call[0] != "self.robot.get_head_angles" for call in gateway.esp32.calls)


@pytest.mark.asyncio
async def test_consume_calls_set_head_angles() -> None:
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(url=_url("consume"), smoothing_window=1)
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)

    await follower._consume(
        _FakeWebSocket([json.dumps({"yaw": 10, "pitch": -5, "ts": 123})])
    )

    assert gateway.esp32.calls == [
        (
            _SET_HEAD_ANGLES,
            {"yaw": 10, "pitch": 40, "speed_dps": 240},
        )
    ]
    assert follower.status()["frames_received"] == 1
    assert follower.status()["frames_accepted"] == 1
    assert follower.status()["last_frame_ts"] == 123


@pytest.mark.asyncio
async def test_consume_skips_command_on_call_result_isError() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _SET_HEAD_ANGLES,
        {"isError": True, "content": [{"text": json.dumps({"error": "failed"})}]},
    )
    cfg = FollowPoseStreamConfig(url=_url("set-is-error"), smoothing_window=1)
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    assert follower.status()["commands_sent"] == 0
    assert follower._last_servo_yaw == 0
    assert follower._last_servo_pitch == 45
    assert follower._last_sent_at is None
    assert follower._last_error == "set_head_angles reported isError"


@pytest.mark.asyncio
async def test_consume_skips_command_on_payload_ok_false() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(_SET_HEAD_ANGLES, _wrap_call_payload({"ok": False}))
    cfg = FollowPoseStreamConfig(url=_url("set-ok-false"), smoothing_window=1)
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    assert follower.status()["commands_sent"] == 0
    assert follower._last_servo_yaw == 0
    assert follower._last_servo_pitch == 45
    assert follower._last_sent_at is None
    assert follower._last_error == "set_head_angles payload reported ok=false"


@pytest.mark.asyncio
async def test_consume_skips_command_on_payload_servo_init_ok_false() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _SET_HEAD_ANGLES,
        _wrap_call_payload({"servo_init_ok": False}),
    )
    cfg = FollowPoseStreamConfig(
        url=_url("set-servo-init-false"),
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    assert follower.status()["commands_sent"] == 0
    assert follower._last_servo_yaw == 0
    assert follower._last_servo_pitch == 45
    assert follower._last_sent_at is None
    assert follower._last_error == (
        "set_head_angles payload reported servo_init_ok=false"
    )


@pytest.mark.asyncio
async def test_consume_advances_on_successful_payload() -> None:
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _SET_HEAD_ANGLES,
        _wrap_call_payload({"servo_init_ok": True}),
    )
    cfg = FollowPoseStreamConfig(url=_url("set-success"), smoothing_window=1)
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 10, "pitch": 0})]))

    assert follower.status()["commands_sent"] == 1
    assert follower._last_servo_yaw == 10
    assert follower._last_servo_pitch == 45
    assert follower._last_sent_at is not None



@pytest.mark.asyncio
async def test_consume_filters_by_source() -> None:
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("source-filter"),
        source_filter="airpods",
    )
    follower = FollowPoseStream(gateway, cfg)

    await follower._consume(
        _FakeWebSocket(
            [json.dumps({"source": "dummy", "yaw": 10, "pitch": 0})]
        )
    )

    assert gateway.esp32.calls == []
    assert follower.status()["frames_accepted"] == 0



@pytest.mark.asyncio
async def test_consume_filters_by_frame() -> None:
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("frame-filter"),
        frame_filter="calibrated",
    )
    follower = FollowPoseStream(gateway, cfg)

    await follower._consume(
        _FakeWebSocket([json.dumps({"frame": "raw", "yaw": 10, "pitch": 0})])
    )

    assert gateway.esp32.calls == []
    assert follower.status()["frames_accepted"] == 0



@pytest.mark.asyncio
async def test_consume_rejects_non_numeric_yaw() -> None:
    gateway = _FakeGateway()
    follower = FollowPoseStream(
        gateway,
        FollowPoseStreamConfig(url=_url("bad-yaw")),
    )

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": "abc", "pitch": 0})]))

    assert gateway.esp32.calls == []
    assert follower.status()["frames_accepted"] == 0



@pytest.mark.asyncio
async def test_consume_rejects_non_dict() -> None:
    gateway = _FakeGateway()
    follower = FollowPoseStream(
        gateway,
        FollowPoseStreamConfig(url=_url("non-dict")),
    )

    await follower._consume(_FakeWebSocket(["42"]))

    assert gateway.esp32.calls == []
    assert follower.status()["frames_accepted"] == 0



@pytest.mark.asyncio
async def test_consume_downsamples(monkeypatch: pytest.MonkeyPatch) -> None:
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("downsample"),
        downsample_hz=10,
        max_step_deg=30,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)
    clock = _Clock()
    loop = asyncio.get_running_loop()
    monkeypatch.setattr(loop, "time", clock.time)
    messages = [json.dumps({"yaw": idx, "pitch": 0}) for idx in range(20)]
    tick_s = 0.01

    await follower._consume(_FakeWebSocket(messages, clock=clock, tick_s=tick_s))

    duration = len(messages) * tick_s
    limit = math.ceil(duration * cfg.downsample_hz) + 1
    assert follower.status()["commands_sent"] <= limit
    assert len(gateway.esp32.calls) == follower.status()["commands_sent"]
    assert follower.status()["commands_sent"] >= 1



@pytest.mark.asyncio
async def test_consume_step_clamps_velocity() -> None:
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("step-clamp"),
        max_step_deg=5,
        smoothing_window=1,
    )
    follower = FollowPoseStream(gateway, cfg)
    _mark_seeded(follower)

    await follower._consume(_FakeWebSocket([json.dumps({"yaw": 100, "pitch": 0})]))

    assert gateway.esp32.calls == [
        (
            _SET_HEAD_ANGLES,
            {"yaw": 5, "pitch": 45, "speed_dps": 240},
        )
    ]



@pytest.mark.asyncio
async def test_start_invokes_wifi_ps_none(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(fps.websockets, "connect", _refusing_connect)
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("wifi-start"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg)

    assert gateway.esp32.calls[0] == (_WIFI_SET_POWER_SAVE, {"mode": "none"})
    await fps.stop_follow()


@pytest.mark.asyncio
async def test_stop_invokes_wifi_ps_min_modem(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(fps.websockets, "connect", _refusing_connect)
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("wifi-stop"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg)
    await fps.stop_follow()

    assert (_WIFI_SET_POWER_SAVE, {"mode": "min_modem"}) in gateway.esp32.calls


@pytest.mark.asyncio
async def test_stop_restores_previous_wifi_ps(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(fps.websockets, "connect", _refusing_connect)
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _WIFI_SET_POWER_SAVE,
        {"ok": True, "previous": "max_modem", "current": "none"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("wifi-restore-previous"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg)

    follower = fps._follower
    assert follower is not None
    assert follower._wifi_ps_previous == "max_modem"
    await fps.stop_follow()

    assert (_WIFI_SET_POWER_SAVE, {"mode": "max_modem"}) in gateway.esp32.calls


@pytest.mark.asyncio
async def test_stop_falls_back_to_min_modem_when_previous_unknown(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(fps.websockets, "connect", _refusing_connect)
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _WIFI_SET_POWER_SAVE,
        {"ok": True, "previous": "unknown", "current": "none"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("wifi-restore-unknown"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg)

    follower = fps._follower
    assert follower is not None
    assert follower._wifi_ps_previous is None
    await fps.stop_follow()

    assert (_WIFI_SET_POWER_SAVE, {"mode": "min_modem"}) in gateway.esp32.calls


@pytest.mark.asyncio
async def test_start_tolerates_wifi_ps_failure(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(fps.websockets, "connect", _refusing_connect)
    gateway = _FakeGateway()
    gateway.esp32.set_reply(
        _WIFI_SET_POWER_SAVE,
        None,
        {"code": -32000, "message": "device offline"},
    )
    cfg = FollowPoseStreamConfig(
        url=_url("wifi-start-failure"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg)

    apply_result = fps.get_follow_status()["wifi_ps_apply_result"]
    assert apply_result["ok"] is False
    assert "device offline" in apply_result["error"]
    await fps.stop_follow()

    assert (_WIFI_SET_POWER_SAVE, {"mode": "min_modem"}) in gateway.esp32.calls


@pytest.mark.asyncio
async def test_stop_tolerates_wifi_ps_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(fps.websockets, "connect", _refusing_connect)
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("wifi-stop-failure"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg)
    gateway.esp32.set_reply(
        _WIFI_SET_POWER_SAVE,
        None,
        {"code": -32000, "message": "device offline"},
    )
    status = await fps.stop_follow()

    restore_result = status["wifi_ps_restore_result"]
    assert restore_result["ok"] is False
    assert "device offline" in restore_result["error"]


def test_extract_wifi_ps_result_shapes() -> None:
    direct = {"ok": True, "previous": "min_modem", "current": "none"}
    envelope = _wrap_call_payload(direct)

    assert FollowPoseStream._extract_wifi_ps_result(direct) == direct
    assert FollowPoseStream._extract_wifi_ps_result(envelope) == direct

    unrecognised = FollowPoseStream._extract_wifi_ps_result({"content": []})
    assert unrecognised["ok"] is False
    assert "error" in unrecognised


@pytest.mark.asyncio
async def test_start_then_start_cancels_previous(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class _RefusingConnect:
        async def __aenter__(self) -> None:
            raise ConnectionRefusedError("refused")

        async def __aexit__(self, *exc_info: object) -> bool:
            return False

    def refusing_connect(_url: str) -> _RefusingConnect:
        return _RefusingConnect()

    monkeypatch.setattr(fps.websockets, "connect", refusing_connect)
    gateway = _FakeGateway()
    cfg_a = FollowPoseStreamConfig(
        url=_url("a"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )
    cfg_b = FollowPoseStreamConfig(
        url=_url("b"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )

    await fps.start_follow(gateway, cfg_a)
    first = fps._follower
    assert first is not None
    first_task = first._task
    assert first_task is not None
    await asyncio.sleep(0)

    await fps.start_follow(gateway, cfg_b)

    assert fps.get_follow_status()["url"] == _url("b")
    assert first_task.done()



@pytest.mark.asyncio
async def test_start_status_returns_local_follower_when_concurrent_stop_clears_singleton(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """F5 fix: even if a concurrent task clears _follower to None
    while start_follow() is mid-await, start_follow() must still
    return a valid status() dict instead of raising AttributeError.

    The lock added by F5 normally prevents that interleaving, but we
    additionally protect start_follow() by holding a local reference
    to the follower it created — this test verifies that local
    reference is what status() is read from.
    """
    class _RefusingConnect:
        async def __aenter__(self) -> None:
            raise ConnectionRefusedError("refused")

        async def __aexit__(self, *exc_info: object) -> bool:
            return False

    def refusing_connect(_url: str) -> _RefusingConnect:
        return _RefusingConnect()

    monkeypatch.setattr(fps.websockets, "connect", refusing_connect)
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("race-start-status"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )
    # Pre-clear so the test starts from a clean module state.
    if fps._follower is not None:
        await fps.stop_follow()
    status = await fps.start_follow(gateway, cfg)
    # Even if something cleared the singleton between the start
    # completing and us reading it, the status returned by
    # start_follow itself must be intact (it uses a local reference).
    assert isinstance(status, dict)
    assert status.get("url") == _url("race-start-status")
    # Clean up.
    await fps.stop_follow()


@pytest.mark.asyncio
async def test_start_stop_serialised_by_singleton_lock(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """F5 fix: a stop_follow() racing against an in-flight
    start_follow() must wait on the singleton lock so the start can
    complete cleanly before the stop runs. After both return, the
    singleton must be cleared (stop won the lock second).
    """
    class _RefusingConnect:
        async def __aenter__(self) -> None:
            raise ConnectionRefusedError("refused")

        async def __aexit__(self, *exc_info: object) -> bool:
            return False

    def refusing_connect(_url: str) -> _RefusingConnect:
        return _RefusingConnect()

    monkeypatch.setattr(fps.websockets, "connect", refusing_connect)
    gateway = _FakeGateway()
    cfg = FollowPoseStreamConfig(
        url=_url("race-start-stop"),
        reconnect_initial_backoff_s=0.01,
        reconnect_max_backoff_s=0.01,
    )
    if fps._follower is not None:
        await fps.stop_follow()

    start_task = asyncio.create_task(fps.start_follow(gateway, cfg))
    # Yield so start_follow has a chance to acquire the lock first.
    await asyncio.sleep(0)
    stop_task = asyncio.create_task(fps.stop_follow())

    start_status = await start_task
    stop_status = await stop_task

    assert isinstance(start_status, dict)
    assert isinstance(stop_status, dict)
    # Both calls returned valid dicts (= no AttributeError).
    # Singleton is cleared because stop ran after start completed.
    assert fps._follower is None


@pytest.mark.asyncio
async def test_stop_when_not_running() -> None:
    assert await fps.stop_follow() == {"running": False}


def test_get_follow_status_when_not_running() -> None:
    assert fps.get_follow_status() == {"running": False}
