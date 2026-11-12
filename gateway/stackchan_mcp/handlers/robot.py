"""Robot handlers: servo and LED (stub implementation with in-memory state)."""

from __future__ import annotations

import logging
from typing import Any

from ..tools import SetHeadAnglesParams, SetLedColorParams

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# In-memory device state
# ---------------------------------------------------------------------------

_head_angles: dict[str, int] = {"yaw": 0, "pitch": 0}
_led_color: dict[str, int] = {"r": 0, "g": 0, "b": 0}


# ---------------------------------------------------------------------------
# Handlers
# ---------------------------------------------------------------------------


def get_head_angles(_args: dict[str, Any] | None = None) -> dict[str, int]:
    """Return current head angles."""
    logger.info("get_head_angles -> %s", _head_angles)
    return dict(_head_angles)


def set_head_angles(args: dict[str, Any]) -> bool:
    """Set head angles (in-memory stub)."""
    params = SetHeadAnglesParams(**args)
    _head_angles["yaw"] = params.yaw
    _head_angles["pitch"] = params.pitch
    logger.info(
        "set_head_angles yaw=%d pitch=%d speed=%d",
        params.yaw,
        params.pitch,
        params.speed,
    )
    return True


def set_led_color(args: dict[str, Any]) -> bool:
    """Set LED color (in-memory stub)."""
    params = SetLedColorParams(**args)
    _led_color["r"] = params.r
    _led_color["g"] = params.g
    _led_color["b"] = params.b
    logger.info("set_led_color r=%d g=%d b=%d", params.r, params.g, params.b)
    return True
