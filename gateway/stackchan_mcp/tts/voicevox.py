"""VOICEVOX engine — HTTP client for a user-run VOICEVOX server.

VOICEVOX itself is LGPL-3.0 licensed but runs as a separate HTTP
process (the official ``voicevox/voicevox_engine`` Docker image is the
recommended setup). The gateway never links VOICEVOX code; it only
issues HTTP requests, so the LGPL terms apply only to the engine
process and not to gateway code, which remains MIT.

By default the engine is reached at ``http://127.0.0.1:50021``. Override
with the ``STACKCHAN_VOICEVOX_URL`` environment variable, or by passing
``url=`` to :class:`VoicevoxEngine` directly (used by tests).
"""

from __future__ import annotations

import logging
import os
from typing import Any

from .audio_utils import (
    DEVICE_SAMPLE_RATE,
    resample_pcm16_linear,
    wav_to_pcm16_mono,
)
from .base import TTSEngine

logger = logging.getLogger(__name__)


#: Default URL of the VOICEVOX HTTP engine. Matches the upstream Docker
#: image's exposed port.
DEFAULT_VOICEVOX_URL = "http://127.0.0.1:50021"

#: Default speaker ID. ``3`` is Zundamon (normal voice), the most
#: commonly used VOICEVOX speaker. Override per-call via the say()
#: tool's ``speaker_id`` argument or globally via the
#: ``STACKCHAN_VOICEVOX_DEFAULT_SPEAKER`` environment variable.
DEFAULT_VOICEVOX_SPEAKER = 3

#: HTTP timeout for both /audio_query and /synthesis. Synthesis can be
#: a few seconds for a long sentence on CPU, so we err on the generous
#: side.
DEFAULT_HTTP_TIMEOUT_SECONDS = 30.0


class VoicevoxEngine(TTSEngine):
    """Synthesise text by POSTing to a running VOICEVOX HTTP engine.

    Setup (recommended): run the official Docker image::

        docker run --rm -p '127.0.0.1:50021:50021' \\
            voicevox/voicevox_engine:cpu-ubuntu20.04-latest

    Configuration:

        ``STACKCHAN_VOICEVOX_URL``
            Base URL of the engine. Default ``http://127.0.0.1:50021``.

        ``STACKCHAN_VOICEVOX_DEFAULT_SPEAKER``
            Integer speaker ID used when the say() tool does not
            specify one. Default ``3`` (Zundamon, normal).
    """

    name = "voicevox"

    def __init__(
        self,
        url: str | None = None,
        default_speaker: int | None = None,
        timeout_seconds: float = DEFAULT_HTTP_TIMEOUT_SECONDS,
        transport: Any = None,
    ) -> None:
        """Construct a VOICEVOX engine.

        ``transport`` is an :class:`httpx.BaseTransport` (or
        compatible) handed straight to :class:`httpx.AsyncClient`.
        Tests pass a :class:`httpx.MockTransport` to avoid hitting the
        network; production callers leave it ``None`` so httpx picks
        its default HTTP transport.
        """
        env_url = os.getenv("STACKCHAN_VOICEVOX_URL")
        self._url = (url or env_url or DEFAULT_VOICEVOX_URL).rstrip("/")

        if default_speaker is not None:
            self._default_speaker = default_speaker
        else:
            env_speaker = os.getenv("STACKCHAN_VOICEVOX_DEFAULT_SPEAKER")
            if env_speaker:
                try:
                    self._default_speaker = int(env_speaker)
                except ValueError:
                    logger.warning(
                        "Invalid STACKCHAN_VOICEVOX_DEFAULT_SPEAKER=%r, "
                        "falling back to %d",
                        env_speaker,
                        DEFAULT_VOICEVOX_SPEAKER,
                    )
                    self._default_speaker = DEFAULT_VOICEVOX_SPEAKER
            else:
                self._default_speaker = DEFAULT_VOICEVOX_SPEAKER

        self._timeout_seconds = timeout_seconds
        self._transport = transport

    @property
    def url(self) -> str:
        """Base URL the engine will connect to. Useful for diagnostics."""
        return self._url

    @property
    def default_speaker(self) -> int:
        """Speaker ID used when ``speaker_id`` is omitted from a call."""
        return self._default_speaker

    async def synthesize(self, text: str, **opts: Any) -> bytes:
        """POST text to VOICEVOX, decode the WAV, return 16 kHz mono PCM.

        Recognised opts:

            ``speaker_id``: int
                VOICEVOX speaker ID. Falls back to
                :attr:`default_speaker` (which itself defaults to
                ``3`` — Zundamon, normal).

        VOICEVOX returns WAV at the speaker's native sample rate
        (24 kHz for the bundled speakers). We resample to
        :data:`audio_utils.DEVICE_SAMPLE_RATE` to match the device's
        Opus decoder.
        """
        try:
            import httpx  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - exercised via integration
            raise RuntimeError(
                "httpx is not installed. Install with "
                "'pip install stackchan-mcp[tts]' to enable VOICEVOX support."
            ) from exc

        if not isinstance(text, str) or not text.strip():
            raise ValueError("VOICEVOX synthesize: 'text' must be a non-empty string")

        speaker_raw = opts.get("speaker_id")
        if speaker_raw is None:
            speaker = self._default_speaker
        else:
            try:
                speaker = int(speaker_raw)
            except (TypeError, ValueError) as exc:
                raise ValueError(
                    f"speaker_id must be an integer, got {speaker_raw!r}"
                ) from exc

        client_kwargs: dict[str, Any] = {"timeout": self._timeout_seconds}
        if self._transport is not None:
            client_kwargs["transport"] = self._transport
        async with httpx.AsyncClient(**client_kwargs) as client:
            # 1. Build an AudioQuery for the text.
            query_resp = await client.post(
                f"{self._url}/audio_query",
                params={"text": text, "speaker": speaker},
            )
            query_resp.raise_for_status()
            audio_query = query_resp.json()

            # 2. Synthesise the WAV from the AudioQuery.
            synth_resp = await client.post(
                f"{self._url}/synthesis",
                params={"speaker": speaker},
                json=audio_query,
            )
            synth_resp.raise_for_status()
            wav_bytes = synth_resp.content

        sample_rate, pcm = wav_to_pcm16_mono(wav_bytes)
        if sample_rate != DEVICE_SAMPLE_RATE:
            pcm = resample_pcm16_linear(pcm, sample_rate, DEVICE_SAMPLE_RATE)

        logger.info(
            "VOICEVOX synthesised %d bytes PCM (16 kHz mono) for "
            "speaker=%d, text=%r",
            len(pcm),
            speaker,
            text[:60],
        )
        return pcm
