"""Refcounted WiFi power-save leases for high-rate gateway streams."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import json
import logging
from typing import Any, Optional

WIFI_PS_STREAM_MODE = "none"
WIFI_PS_IDLE_MODE = "min_modem"
_WIFI_SET_POWER_SAVE = "self.wifi.set_power_save"

logger = logging.getLogger(__name__)


@dataclass
class _PowerSaveState:
    client: Any
    ref_count: int = 0
    previous: str | None = None
    apply_result: dict[str, Any] | None = None


_lock: asyncio.Lock | None = None
_states: dict[int, _PowerSaveState] = {}


def _get_lock() -> asyncio.Lock:
    global _lock
    if _lock is None:
        _lock = asyncio.Lock()
    return _lock


def _state_key(client: Any) -> int:
    return id(client)


def _remember_previous(
    state: _PowerSaveState,
    result: dict[str, Any],
) -> None:
    previous = result.get("previous")
    if not isinstance(previous, str) or not previous or previous == "unknown":
        return
    if state.previous is None:
        state.previous = previous


async def acquire_wifi_power_save(client: Any) -> dict[str, Any]:
    """Acquire stream-mode WiFi power save, forcing PS=none on first holder."""
    async with _get_lock():
        key = _state_key(client)
        state = _states.get(key)
        if state is None:
            state = _PowerSaveState(client=client)
            _states[key] = state

        state.ref_count += 1
        if state.ref_count == 1:
            result = await _set_power_save(client, WIFI_PS_STREAM_MODE)
            state.apply_result = result
            _remember_previous(state, result)
            return result

        if not (
            isinstance(state.apply_result, dict)
            and state.apply_result.get("ok")
        ):
            result = await _set_power_save(client, WIFI_PS_STREAM_MODE)
            state.apply_result = result
            _remember_previous(state, result)
            return result

        return {
            **state.apply_result,
            "ref_count": state.ref_count,
            "shared": True,
        }


async def reapply_wifi_power_save(client: Any) -> dict[str, Any]:
    """Re-send PS=none after a device reconnect if the active lease failed."""
    async with _get_lock():
        result = await _set_power_save(client, WIFI_PS_STREAM_MODE)
        state = _states.get(_state_key(client))
        if state is not None and state.ref_count > 0:
            state.apply_result = result
            _remember_previous(state, result)
        return result


async def release_wifi_power_save(client: Any) -> dict[str, Any]:
    """Release a stream-mode lease, restoring the saved mode on last holder."""
    async with _get_lock():
        key = _state_key(client)
        state = _states.get(key)
        if state is None or state.ref_count <= 0:
            return {
                "ok": True,
                "skipped": True,
                "reason": "not_acquired",
                "current": "unknown",
            }

        state.ref_count -= 1
        if state.ref_count > 0:
            return {
                "ok": True,
                "skipped": True,
                "ref_count": state.ref_count,
                "previous": state.previous,
                "current": WIFI_PS_STREAM_MODE,
            }

        restore_mode = state.previous or WIFI_PS_IDLE_MODE
        result = await _set_power_save(client, restore_mode)
        _states.pop(key, None)
        return result


async def _set_power_save(client: Any, mode: str) -> dict[str, Any]:
    """Best-effort WiFi PS set. Never raises."""
    try:
        result, error = await client.call_tool(_WIFI_SET_POWER_SAVE, {"mode": mode})
    except Exception as exc:
        message = f"call_raised: {exc}"
        logger.warning("wifi power-save set failed: %s", message)
        return {"ok": False, "error": message}

    if error:
        logger.warning("wifi power-save set returned error: %s", error)
        return {"ok": False, "error": str(error)}

    parsed = extract_wifi_power_save_result(result)
    if not parsed.get("ok"):
        logger.warning("wifi power-save set returned non-ok result: %s", parsed)
    return parsed


def extract_wifi_power_save_result(result: Any) -> dict[str, Any]:
    if isinstance(result, dict):
        if "ok" in result:
            parsed = {
                "ok": bool(result.get("ok")),
                "previous": result.get("previous"),
                "current": result.get("current"),
            }
            if "error" in result:
                parsed["error"] = result.get("error")
            return parsed
        payload = _decode_call_result_payload(result)
        if isinstance(payload, dict):
            parsed = {
                "ok": bool(payload.get("ok")),
                "previous": payload.get("previous"),
                "current": payload.get("current"),
            }
            if "error" in payload:
                parsed["error"] = payload.get("error")
            return parsed
    return {"ok": False, "error": "unrecognised result shape"}


def _decode_call_result_payload(result: dict[str, Any]) -> Optional[dict[str, Any]]:
    if "content" not in result:
        return result
    content = result.get("content")
    if not isinstance(content, list) or not content:
        return None
    first = content[0]
    if not isinstance(first, dict):
        return None
    text = first.get("text")
    if not isinstance(text, str):
        return None
    try:
        payload = json.loads(text)
    except (TypeError, ValueError):
        return None
    if not isinstance(payload, dict):
        return None
    return payload


def _clear_for_tests() -> None:
    _states.clear()
