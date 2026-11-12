"""Audio handlers: volume control (stub)."""

from __future__ import annotations

import logging
from typing import Any

from ..tools import SetVolumeParams

logger = logging.getLogger(__name__)

_volume: int = 50


def set_volume(args: dict[str, Any]) -> bool:
    """Set speaker volume (in-memory stub)."""
    global _volume
    params = SetVolumeParams(**args)
    _volume = params.volume
    logger.info("set_volume -> %d", _volume)
    return True
