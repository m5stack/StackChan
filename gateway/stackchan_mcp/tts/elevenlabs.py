"""ElevenLabs engine — HTTP client for the ElevenLabs text-to-speech API.

Follows the same thin-client shape as the Irodori engine: issue an HTTP
request, receive an MP3 blob, decode to 16 kHz mono PCM, and hand the PCM
to the orchestrator (which owns Opus encoding and the WebSocket push).

Configuration (environment variables):

    ``ELEVENLABS_API_KEY`` / ``STACKCHAN_ELEVENLABS_KEY``
        API key. Read from the environment only; never commit it. When
        both are set, ``STACKCHAN_ELEVENLABS_KEY`` wins (the more
        specific name overrides a machine-wide key).

    ``STACKCHAN_ELEVEN_VOICE_<SPEAKER>``
        Voice-ID map. Each variable defines one named speaker: e.g.
        ``STACKCHAN_ELEVEN_VOICE_RACHEL`` / ``STACKCHAN_ELEVEN_VOICE_ADAM``.
        The ``speaker_id`` call option selects by lowercase suffix
        (``"rachel"`` / ``"adam"``); an unrecognised ``speaker_id`` that
        looks like a raw ElevenLabs voice ID is passed through verbatim.

    ``STACKCHAN_ELEVEN_DEFAULT_SPEAKER``
        Speaker name used when ``speaker_id`` is omitted. When unset and
        exactly one voice is configured, that voice is the default.

    ``STACKCHAN_ELEVEN_MODEL``
        Model identifier. Default ``"eleven_v3"``.
"""

from __future__ import annotations

import logging
import os
import re
from typing import Any

from .audio_utils import (
    DEVICE_SAMPLE_RATE,
    _decode_mp3_to_pcm16_mono,
    resample_pcm16_linear,
)
from .base import TTSEngine

logger = logging.getLogger(__name__)

DEFAULT_ELEVEN_MODEL = "eleven_v3"

#: Synthesis latency on the ElevenLabs API can reach several seconds for
#: longer sentences; err on the generous side like the other engines.
DEFAULT_HTTP_TIMEOUT_SECONDS = 40.0

_VOICE_ENV_PREFIX = "STACKCHAN_ELEVEN_VOICE_"

#: A raw ElevenLabs voice ID is ~20 chars of base62; accept and pass
#: through anything that matches so callers can address unmapped voices.
_RAW_VOICE_ID_RE = re.compile(r"^[A-Za-z0-9]{16,32}$")


class ElevenLabsEngine(TTSEngine):
    """Synthesise text via the ElevenLabs ``/v1/text-to-speech`` API."""

    name = "elevenlabs"
    supports_emoji_style = False

    def __init__(self, *, timeout_seconds: float = DEFAULT_HTTP_TIMEOUT_SECONDS, transport: Any = None) -> None:
        # Key / voices / model resolve lazily at synthesis time so values
        # loaded from .env after import still take effect (same rationale
        # as the Irodori engine).
        self._timeout_seconds = timeout_seconds
        self._transport = transport

    @staticmethod
    def _resolve_api_key() -> str:
        key = (os.getenv("STACKCHAN_ELEVENLABS_KEY") or os.getenv("ELEVENLABS_API_KEY") or "").strip()
        if not key:
            raise RuntimeError(
                "ElevenLabs API key is not configured. Set ELEVENLABS_API_KEY "
                "(or STACKCHAN_ELEVENLABS_KEY) in the gateway environment."
            )
        return key

    @staticmethod
    def _voice_map() -> dict[str, str]:
        """Return {speaker-name-lowercase: voice_id} from the environment."""
        voices: dict[str, str] = {}
        for env_name, value in os.environ.items():
            if env_name.startswith(_VOICE_ENV_PREFIX) and value.strip():
                voices[env_name[len(_VOICE_ENV_PREFIX):].lower()] = value.strip()
        return voices

    def _resolve_voice_id(self, speaker_raw: Any) -> tuple[str, str]:
        """Map a ``speaker_id`` option to ``(speaker_label, voice_id)``."""
        voices = self._voice_map()
        default_speaker = (os.getenv("STACKCHAN_ELEVEN_DEFAULT_SPEAKER") or "").strip().lower()
        if not default_speaker and len(voices) == 1:
            default_speaker = next(iter(voices))

        speaker = str(speaker_raw).strip().lower() if speaker_raw is not None else default_speaker
        if speaker and speaker in voices:
            return speaker, voices[speaker]
        if speaker_raw is not None and _RAW_VOICE_ID_RE.match(str(speaker_raw).strip()):
            return "raw", str(speaker_raw).strip()
        if default_speaker in voices:
            logger.warning(
                "ElevenLabs speaker %r not found; falling back to default %r",
                speaker_raw,
                default_speaker,
            )
            return default_speaker, voices[default_speaker]
        raise RuntimeError(
            f"ElevenLabs speaker {speaker_raw!r} is not configured and no default "
            f"voice is available. Define STACKCHAN_ELEVEN_VOICE_<NAME> variables "
            f"(known: {sorted(voices) or 'none'})."
        )

    async def synthesize(self, text: str, **opts: Any) -> bytes:
        """Call ElevenLabs, decode the MP3, return 16 kHz mono PCM.

        Recognised opts:

            ``speaker_id``
                Named speaker (matched against the env voice map) or a
                raw ElevenLabs voice ID.
        """
        try:
            import httpx  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - exercised via integration
            raise RuntimeError(
                "httpx is not installed. Install with "
                "'pip install stackchan-mcp[tts-elevenlabs]' to enable "
                "ElevenLabs support."
            ) from exc

        if not isinstance(text, str) or not text.strip():
            raise ValueError("ElevenLabs synthesize: 'text' must be a non-empty string")

        api_key = self._resolve_api_key()
        # speaker_name is the string channel in the say tool schema
        # (speaker_id is typed integer there for VOICEVOX); accept both.
        speaker_opt = opts.get("speaker_name")
        if speaker_opt is None:
            speaker_opt = opts.get("speaker_id")
        speaker, voice_id = self._resolve_voice_id(speaker_opt)
        model_id = (os.getenv("STACKCHAN_ELEVEN_MODEL") or DEFAULT_ELEVEN_MODEL).strip()

        client_kwargs: dict[str, Any] = {"timeout": self._timeout_seconds}
        if self._transport is not None:
            client_kwargs["transport"] = self._transport

        async with httpx.AsyncClient(**client_kwargs) as client:
            resp = await client.post(
                f"https://api.elevenlabs.io/v1/text-to-speech/{voice_id}",
                headers={"xi-api-key": api_key},
                json={"text": text, "model_id": model_id},
            )
        if resp.status_code != 200:
            raise RuntimeError(
                f"ElevenLabs synthesis failed: HTTP {resp.status_code} {resp.text[:200]!r}"
            )
        mp3_bytes = resp.content
        if not mp3_bytes:
            raise RuntimeError("ElevenLabs returned an empty audio body.")

        sample_rate, pcm = _decode_mp3_to_pcm16_mono(mp3_bytes)
        if sample_rate != DEVICE_SAMPLE_RATE:
            pcm = resample_pcm16_linear(pcm, sample_rate, DEVICE_SAMPLE_RATE)

        logger.info(
            "ElevenLabs synthesised %d bytes PCM (16 kHz mono) for speaker=%s, model=%s, text=%r",
            len(pcm),
            speaker,
            model_id,
            text[:60],
        )
        return pcm
