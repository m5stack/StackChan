"""faster-whisper engine — local STT with no cloud round-trip.

faster-whisper is a CTranslate2 reimplementation of OpenAI Whisper that
runs well on CPU and is the default engine for this gateway (see
Issue #91). The library is MIT-licensed and the bundled models are
downloaded on first use from the Hugging Face Hub.

By default the ``base`` multilingual model is loaded — small enough to
fit on a developer Mac without GPU and accurate enough for short
Japanese utterances. Override per-call via the ``model`` argument of
the ``listen`` MCP tool, or globally via the ``STACKCHAN_FASTER_WHISPER_MODEL``
environment variable.
"""

from __future__ import annotations

import asyncio
import logging
import os
import wave
from io import BytesIO
from typing import Any

# Probe the optional dependency at module import time so a missing
# extra produces ImportError here, which :mod:`stackchan_mcp.stt`'s
# ``_try_register`` swallows cleanly. Without this probe the engine
# would register successfully (we import faster_whisper lazily inside
# :meth:`FasterWhisperEngine._load_model`), and a ``listen()`` call
# would put the device into recording mode + wait for the entire
# capture window before failing at decode time — surfacing a stale
# "install [stt-faster-whisper]" error to the agent only after the
# user had already spoken into the dead capture.
import faster_whisper as _faster_whisper  # noqa: F401  (probe-only import)

from .audio_utils import DEVICE_SAMPLE_RATE
from .base import STTEngine

logger = logging.getLogger(__name__)


#: Default faster-whisper model identifier. ``base`` is the smallest
#: multilingual model that still handles short Japanese utterances
#: reliably.
DEFAULT_FASTER_WHISPER_MODEL = "base"

#: Default compute type. ``int8`` keeps memory low and runs fast on
#: modern Mac / Linux CPUs; users with a GPU can override the engine
#: directly to use ``float16`` etc.
DEFAULT_COMPUTE_TYPE = "int8"

#: Default device. faster-whisper picks CPU automatically when CUDA is
#: not available; we set it explicitly so the engine doesn't probe.
DEFAULT_DEVICE = "cpu"


def _pcm_to_wav_bytes(pcm: bytes, sample_rate: int) -> bytes:
    """Wrap raw signed-16-bit mono PCM in a WAV container in memory.

    faster-whisper's transcribe() accepts a file-like object pointing
    at a decodable audio container; piping raw PCM bytes through a
    short WAV header is simpler than learning the library's NumPy
    array path and avoids a numpy dependency in this module.
    """
    buf = BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)  # 16-bit
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)
    return buf.getvalue()


class FasterWhisperEngine(STTEngine):
    """Transcribe via faster-whisper running locally.

    Setup:

        pip install stackchan-mcp[stt-faster-whisper]

    The first call downloads the model from the Hugging Face Hub and
    caches it under ``~/.cache/huggingface/hub``; subsequent calls
    reuse the cached model and the constructor's lazy-init keeps
    startup time bounded.

    Configuration:

        ``STACKCHAN_FASTER_WHISPER_MODEL``
            Model identifier (``tiny`` / ``base`` / ``small`` /
            ``medium`` / ``large-v3``). Default ``base``.
        ``STACKCHAN_FASTER_WHISPER_DEVICE``
            ``cpu`` (default) / ``cuda`` / ``auto``.
        ``STACKCHAN_FASTER_WHISPER_COMPUTE_TYPE``
            ``int8`` (default) / ``float16`` / ``float32``.
    """

    name = "faster-whisper"

    def __init__(
        self,
        model: str | None = None,
        device: str | None = None,
        compute_type: str | None = None,
    ) -> None:
        env_model = os.getenv("STACKCHAN_FASTER_WHISPER_MODEL")
        env_device = os.getenv("STACKCHAN_FASTER_WHISPER_DEVICE")
        env_compute = os.getenv("STACKCHAN_FASTER_WHISPER_COMPUTE_TYPE")

        self._model_name = model or env_model or DEFAULT_FASTER_WHISPER_MODEL
        self._device = device or env_device or DEFAULT_DEVICE
        self._compute_type = compute_type or env_compute or DEFAULT_COMPUTE_TYPE
        self._model: Any = None
        self._load_lock = asyncio.Lock()

    @property
    def model_name(self) -> str:
        """Current model identifier. Useful for diagnostics."""
        return self._model_name

    async def _load_model(self) -> Any:
        """Lazy-load the underlying ``WhisperModel`` once per process.

        Loading takes a couple of seconds on first call (downloads +
        weight init); the lock keeps two concurrent ``listen()`` calls
        from racing the same expensive init.
        """
        if self._model is not None:
            return self._model
        async with self._load_lock:
            if self._model is not None:
                return self._model
            # Top-of-module probe import already guarantees the extra
            # is present; pull the concrete class out here at first
            # use so model construction stays lazy.
            from faster_whisper import WhisperModel  # type: ignore[import-not-found]

            logger.info(
                "Loading faster-whisper model=%s device=%s compute_type=%s",
                self._model_name,
                self._device,
                self._compute_type,
            )
            # Model init is blocking; off-load to a thread so the
            # asyncio loop keeps pumping (gateway also handles ESP32
            # frames concurrently while we're loading on first use).
            self._model = await asyncio.to_thread(
                WhisperModel,
                self._model_name,
                device=self._device,
                compute_type=self._compute_type,
            )
            return self._model

    async def transcribe(self, pcm: bytes, **opts: Any) -> dict[str, Any]:
        """Transcribe PCM via faster-whisper.

        Recognised opts:

            ``language``: str | None
                ISO 639-1 code (e.g. ``"ja"``). ``None`` enables
                language autodetection.
            ``model``: str
                Per-call model override. Triggers a re-init the first
                time a new model is requested (rare); subsequent calls
                with the same value reuse the cached model.
        """
        if not pcm:
            raise ValueError("faster-whisper transcribe: empty PCM buffer")

        # Allow per-call model override. Common case (no override) hits
        # the lazy-loaded model directly.
        override = opts.get("model")
        if isinstance(override, str) and override and override != self._model_name:
            logger.info(
                "Switching faster-whisper model: %s -> %s",
                self._model_name,
                override,
            )
            self._model_name = override
            self._model = None

        model = await self._load_model()

        language_raw = opts.get("language", "ja")
        language: str | None
        if language_raw is None:
            language = None
        elif isinstance(language_raw, str) and language_raw:
            language = language_raw
        else:
            language = "ja"

        wav_bytes = _pcm_to_wav_bytes(pcm, DEVICE_SAMPLE_RATE)

        def _run() -> dict[str, Any]:
            segments, info = model.transcribe(
                BytesIO(wav_bytes),
                language=language,
                beam_size=1,
                vad_filter=False,
            )
            # ``segments`` is a generator; materialising it concatenates
            # all chunks of the transcription.
            text = "".join(seg.text for seg in segments).strip()
            return {
                "text": text,
                "language": info.language or (language or ""),
                "language_probability": float(info.language_probability),
            }

        result = await asyncio.to_thread(_run)
        logger.info(
            "faster-whisper transcribed pcm_bytes=%d language=%s text=%r",
            len(pcm),
            result["language"],
            result["text"][:80],
        )
        return result
