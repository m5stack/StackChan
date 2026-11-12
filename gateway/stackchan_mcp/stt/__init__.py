"""STT framework for Phase 4 (Issue #91).

Companion to :mod:`stackchan_mcp.tts`: this package provides the
engine-agnostic skeleton for the gateway-side ``listen(duration_ms)``
MCP tool plus the concrete faster-whisper (default, local) and
OpenAI Whisper API engines.

Engines whose modules require optional extras to import are registered
behind ``try / except ImportError`` so the framework still works when
the corresponding extra is missing.
"""

from __future__ import annotations

import logging
from typing import Callable

from .base import EngineRegistry, STTEngine, get_registry
from .orchestrator import DEFAULT_ENGINE, listen_and_transcribe

_logger = logging.getLogger(__name__)


def _try_register(register_fn: Callable[[], None], engine_label: str) -> None:
    """Run ``register_fn`` and swallow ImportErrors.

    Used so an engine whose top-level module needs an optional extra
    (e.g. faster-whisper / openai) can fail to register cleanly without
    breaking the rest of the framework. Engine modules themselves
    import cleanly; their heavy dependencies are imported lazily inside
    :meth:`STTEngine.transcribe` so this layer just lights up the
    registry slot.
    """
    try:
        register_fn()
    except ImportError as exc:
        _logger.debug("Skipping %s engine registration: %s", engine_label, exc)


def _register_faster_whisper() -> None:
    from .faster_whisper import FasterWhisperEngine

    get_registry().register(FasterWhisperEngine())


def _register_openai_whisper() -> None:
    from .openai_whisper import OpenAIWhisperEngine

    get_registry().register(OpenAIWhisperEngine())


_try_register(_register_faster_whisper, "faster-whisper")
_try_register(_register_openai_whisper, "openai-whisper")


__all__ = [
    "DEFAULT_ENGINE",
    "EngineRegistry",
    "STTEngine",
    "get_registry",
    "listen_and_transcribe",
]
