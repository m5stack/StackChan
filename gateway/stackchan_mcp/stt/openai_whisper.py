"""OpenAI Whisper API engine — STT via OpenAI's hosted Whisper service.

Useful for users without local compute (e.g. running the gateway on a
Raspberry Pi). Requires an ``OPENAI_API_KEY`` environment variable.
The current OpenAI Whisper endpoint exposed by the official Python SDK
is ``client.audio.transcriptions.create`` with ``model="whisper-1"``.

Latency is dominated by upload + cloud RTT, so the under-2-second
acceptance target from Issue #91 does NOT apply to this engine — it's
documented as cloud-bound. Users can pick this engine when local
faster-whisper is too heavy for their hardware.
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
# ``_try_register`` swallows cleanly. See the matching comment in
# :mod:`stackchan_mcp.stt.faster_whisper` for why this matters: a
# late ImportError at transcribe() time would surface only *after*
# the device had been driven into recording mode for the full
# capture window.
import openai as _openai  # noqa: F401  (probe-only import)

from .audio_utils import DEVICE_SAMPLE_RATE
from .base import STTEngine

logger = logging.getLogger(__name__)


#: Default OpenAI Whisper model. ``whisper-1`` is the only model
#: currently exposed by the public API.
DEFAULT_OPENAI_MODEL = "whisper-1"


def _pcm_to_wav_bytes(pcm: bytes, sample_rate: int) -> bytes:
    """Wrap raw signed-16-bit mono PCM in a WAV container in memory."""
    buf = BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)
    return buf.getvalue()


class OpenAIWhisperEngine(STTEngine):
    """Transcribe via OpenAI's Whisper API.

    Setup:

        pip install stackchan-mcp[stt-openai]
        export OPENAI_API_KEY=sk-...

    Configuration:

        ``OPENAI_API_KEY``
            API key. Required.
        ``STACKCHAN_OPENAI_WHISPER_MODEL``
            Model identifier. Default ``whisper-1``.
        ``OPENAI_BASE_URL``
            Override for OpenAI-compatible providers (Groq Whisper,
            Together, Azure OpenAI, etc.). The SDK reads this env var
            directly when present.
    """

    name = "openai-whisper"

    def __init__(self, model: str | None = None, client: Any = None) -> None:
        env_model = os.getenv("STACKCHAN_OPENAI_WHISPER_MODEL")
        self._model_name = model or env_model or DEFAULT_OPENAI_MODEL
        # ``client`` is injected by tests with a fake; production callers
        # leave it ``None`` so the engine builds the real AsyncOpenAI
        # client lazily inside :meth:`transcribe` (avoiding eager
        # network/auth setup at import time).
        self._injected_client = client

    @property
    def model_name(self) -> str:
        return self._model_name

    async def transcribe(self, pcm: bytes, **opts: Any) -> dict[str, Any]:
        """Transcribe PCM via the OpenAI Whisper API.

        Recognised opts:

            ``language``: str | None
                ISO 639-1 code. ``None`` enables autodetection on the
                API side.
            ``model``: str
                Per-call model override.
        """
        if not pcm:
            raise ValueError("openai-whisper transcribe: empty PCM buffer")

        if self._injected_client is None:
            # Top-of-module probe import already guarantees the extra
            # is present; defer the concrete class binding to first
            # use so API client construction stays lazy.
            from openai import AsyncOpenAI  # type: ignore[import-not-found]

            if not os.getenv("OPENAI_API_KEY"):
                raise RuntimeError(
                    "OPENAI_API_KEY is not set. The OpenAI Whisper engine "
                    "needs an API key; export OPENAI_API_KEY or pick a "
                    "different engine (e.g. engine='faster-whisper')."
                )
            client = AsyncOpenAI()
        else:
            client = self._injected_client

        override = opts.get("model")
        if isinstance(override, str) and override:
            model_name = override
        else:
            model_name = self._model_name

        language_raw = opts.get("language", "ja")
        language: str | None
        if language_raw is None:
            language = None
        elif isinstance(language_raw, str) and language_raw:
            language = language_raw
        else:
            language = "ja"

        wav_bytes = _pcm_to_wav_bytes(pcm, DEVICE_SAMPLE_RATE)
        wav_file = BytesIO(wav_bytes)
        # The OpenAI SDK looks at the file's ``name`` attribute to pick
        # a MIME type; BytesIO lacks one by default. Set a fake name so
        # the upload is recognised as a WAV.
        wav_file.name = "stackchan_listen.wav"

        kwargs: dict[str, Any] = {
            "model": model_name,
            "file": wav_file,
            "response_format": "verbose_json",
        }
        if language is not None:
            kwargs["language"] = language

        # The SDK call is itself async; offload defensively in case a
        # synchronous OpenAI() client is injected by a test.
        coro = client.audio.transcriptions.create(**kwargs)
        if asyncio.iscoroutine(coro):
            response = await coro
        else:
            response = await asyncio.to_thread(lambda: coro)

        # ``verbose_json`` exposes ``text`` and ``language`` attributes
        # on the response. The SDK returns a pydantic model in v1+; fall
        # back to dict access for hand-rolled fakes.
        text = getattr(response, "text", None)
        if text is None and isinstance(response, dict):
            text = response.get("text", "")
        detected = getattr(response, "language", None)
        if detected is None and isinstance(response, dict):
            detected = response.get("language", "")

        result = {
            "text": (text or "").strip(),
            "language": detected or (language or ""),
        }
        logger.info(
            "openai-whisper transcribed pcm_bytes=%d language=%s text=%r",
            len(pcm),
            result["language"],
            result["text"][:80],
        )
        return result
