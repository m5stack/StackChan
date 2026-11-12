"""STT engine abstraction.

Each concrete engine takes 16 kHz mono PCM (signed 16-bit LE) and
returns a transcription. Opus decoding from the device wire format and
PCM buffering are handled by :mod:`stackchan_mcp.stt.orchestrator` so
engines stay focused on recognition.

This module is intentionally dependency-free: it must import cleanly
without ``faster-whisper`` / ``openai`` / ``opuslib`` so that callers
can introspect the registered engines even when the optional ``[stt]``
extras are not installed. Mirrors :mod:`stackchan_mcp.tts.base`.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any


class STTEngine(ABC):
    """Abstract base for STT engines.

    Subclasses must set :attr:`name` to a stable identifier (matched
    against the ``engine`` argument of the ``listen`` MCP tool) and
    implement :meth:`transcribe`.
    """

    #: Stable identifier used to look this engine up in the registry.
    #: Concrete subclasses must override with a non-empty string.
    name: str = ""

    @abstractmethod
    async def transcribe(self, pcm: bytes, **opts: Any) -> dict[str, Any]:
        """Transcribe 16 kHz mono PCM (signed 16-bit LE) into text.

        Args:
            pcm: Raw PCM bytes at 16 kHz, mono, signed 16-bit
                little-endian. The orchestrator handles Opus decoding
                and frame concatenation before calling this method.
            **opts: Engine-specific options. Recognised keys include
                ``language`` (ISO 639-1 code, e.g. ``"ja"``, or
                ``None`` for autodetect) and ``model`` (engine-specific
                model name, e.g. ``"base"`` / ``"small"`` for
                faster-whisper). Engines should ignore unknown options
                rather than raise, so the ``listen`` tool can pass a
                uniform argument set.

        Returns:
            Dict with at least ``text`` (transcribed string) and
            ``language`` (ISO 639-1 code that the engine used or
            detected). Engines may add extra keys (e.g. ``segments``,
            ``confidence``) for diagnostics — the orchestrator surfaces
            ``text`` and ``language`` to the caller and leaves the rest
            available for future extensions.
        """


class EngineRegistry:
    """Tracks available STT engines by name.

    Concrete engines register themselves at import time when their
    optional dependencies are satisfied (see
    :mod:`stackchan_mcp.stt.faster_whisper` and
    :mod:`stackchan_mcp.stt.openai_whisper`).
    """

    def __init__(self) -> None:
        self._engines: dict[str, STTEngine] = {}

    def register(self, engine: STTEngine) -> None:
        """Register ``engine`` under ``engine.name``.

        Replaces any previously registered engine with the same name —
        this is intentional so tests can inject fakes.
        """
        if not engine.name:
            raise ValueError("STTEngine.name must be a non-empty string")
        self._engines[engine.name] = engine

    def get(self, name: str) -> STTEngine | None:
        """Return the engine registered under ``name``, or ``None``."""
        return self._engines.get(name)

    def names(self) -> list[str]:
        """Return all registered engine names, sorted alphabetically."""
        return sorted(self._engines.keys())


_default_registry = EngineRegistry()


def get_registry() -> EngineRegistry:
    """Return the process-wide default :class:`EngineRegistry`."""
    return _default_registry
