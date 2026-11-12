"""Irodori engine — HTTP client for a user-run Irodori synthesis API.

Irodori is a separate text-to-speech service that returns an MP3 stream.
The gateway never links Irodori code; it only issues an HTTP request and
fetches the resulting MP3, so the engine stays a thin client and the
gateway remains MIT-licensed.

Unlike the VOICEVOX engine, there is **no default endpoint URL**. The
synthesis backend is a third-party hosted service, and hard-coding a
specific deployment would point every user at someone else's private
instance. ``STACKCHAN_IRODORI_URL`` is therefore required: the engine
still registers (so it shows up in the engine list) when the variable is
unset, but :meth:`IrodoriEngine.synthesize` fails with a clear error
telling the user to point it at their own deployment.

Configuration (environment variables):

    ``STACKCHAN_IRODORI_URL``
        Synthesis endpoint URL. **Required** — no default. Self-host a
        compatible synthesis API (e.g. duplicate the reference Hugging
        Face Space) and set this to its URL.

    ``STACKCHAN_IRODORI_KEY``
        Optional API key, forwarded as the ``key`` query parameter.
        Read from the environment only; never commit it.

    ``STACKCHAN_IRODORI_SPEAKER``
        Default speaker identifier. Default ``"3"``.

    ``STACKCHAN_IRODORI_STEPS``
        Default diffusion step count. Default ``"24"``.
"""

from __future__ import annotations

import logging
import os
from typing import Any

from .audio_utils import (
    DEVICE_SAMPLE_RATE,
    _decode_mp3_to_pcm16_mono,
    resample_pcm16_linear,
)
from .base import TTSEngine

logger = logging.getLogger(__name__)


#: Default speaker identifier used when ``speaker_id`` is omitted from a
#: call. Sent verbatim as the ``speaker`` query parameter, so it is kept
#: as a string to match the HTTP contract.
DEFAULT_IRODORI_SPEAKER = "3"

#: Default diffusion step count. Higher values trade synthesis latency
#: for quality. Sent verbatim as the ``steps`` query parameter.
DEFAULT_IRODORI_STEPS = "24"

#: HTTP timeout for both the synthesis request and the MP3 fetch.
#: Synthesis on a cold backend can take several seconds, so this errs on
#: the generous side (matching the VOICEVOX engine's default).
DEFAULT_HTTP_TIMEOUT_SECONDS = 30.0


class IrodoriEngine(TTSEngine):
    """Synthesise text by calling a running Irodori synthesis HTTP API.

    The endpoint returns JSON describing where to fetch the synthesised
    MP3; the engine fetches it, decodes it to 16 kHz mono PCM, and hands
    the PCM back to the orchestrator (which owns Opus encoding and the
    WebSocket push).

    Setup: self-host a compatible synthesis API (e.g. duplicate the
    reference Hugging Face Space, or run your own service that honours the
    same request/response contract) and set ``STACKCHAN_IRODORI_URL`` to
    its URL.

    HTTP contract::

        GET {url}?text=<text>&speaker=<speaker>&steps=<steps>
            [&seconds=<seconds>][&key=<key>]

        -> 200 JSON {
               "success": bool,
               "mp3StreamingUrl": str | null,
               "mp3DownloadUrl": str | null,
               "error": str | null
           }

    A non-200 response or ``success: false`` is treated as an engine
    failure, using the server-provided ``error`` text when present.
    """

    name = "irodori"
    supports_emoji_style = True

    def __init__(
        self,
        url: str | None = None,
        *,
        api_key: str | None = None,
        default_speaker: str | None = None,
        default_steps: str | None = None,
        timeout_seconds: float = DEFAULT_HTTP_TIMEOUT_SECONDS,
        transport: Any = None,
    ) -> None:
        """Construct an Irodori engine.

        Args:
            url: Synthesis endpoint URL. When ``None`` the
                ``STACKCHAN_IRODORI_URL`` environment variable is read at
                synthesis time. There is no default URL; an unset URL is
                reported as a clear error when :meth:`synthesize` runs.
            api_key: Optional API key. When ``None`` the
                ``STACKCHAN_IRODORI_KEY`` environment variable is read.
            default_speaker: Speaker identifier used when ``speaker_id``
                is omitted from a call. Falls back to
                ``STACKCHAN_IRODORI_SPEAKER`` then
                :data:`DEFAULT_IRODORI_SPEAKER`.
            default_steps: Diffusion step count used when ``steps`` is
                omitted. Falls back to ``STACKCHAN_IRODORI_STEPS`` then
                :data:`DEFAULT_IRODORI_STEPS`.
            timeout_seconds: HTTP timeout for the synthesis request and
                the MP3 fetch.
            transport: An :class:`httpx.BaseTransport` (or compatible)
                handed straight to :class:`httpx.AsyncClient`. Tests pass
                a :class:`httpx.MockTransport` to avoid hitting the
                network; production callers leave it ``None``.
        """
        # The URL is intentionally resolved lazily (at synthesis time) so
        # that registration never fails just because the variable is
        # unset — the engine stays visible in the registry, and the
        # missing-URL condition becomes an actionable synthesis-time
        # error instead.
        #
        # Only surrounding whitespace is stripped. The value is the
        # synthesis *endpoint* URL (not a base URL), so a trailing slash
        # is significant: stripping it would request a different path on
        # strict-routing deployments.
        self._url_override = url.strip() if url and url.strip() else None
        self._api_key_override = api_key

        # Speaker / steps defaults are resolved lazily (see the
        # properties below) for the same reason as the URL: with
        # ``serve --transport streamable-http`` this engine is
        # constructed at import time, before ``.env`` is loaded, so
        # capturing the environment here would silently ignore
        # dotenv-provided values. Only explicit constructor overrides
        # are pinned at construction time.
        self._default_speaker_override = default_speaker
        self._default_steps_override = default_steps

        self._timeout_seconds = timeout_seconds
        self._transport = transport

    @property
    def default_speaker(self) -> str:
        """Speaker identifier used when ``speaker_id`` is omitted.

        Resolved lazily — constructor override, then
        ``STACKCHAN_IRODORI_SPEAKER``, then
        :data:`DEFAULT_IRODORI_SPEAKER` — so values loaded from ``.env``
        after import still take effect.
        """
        if self._default_speaker_override is not None:
            return self._default_speaker_override
        return os.getenv("STACKCHAN_IRODORI_SPEAKER") or DEFAULT_IRODORI_SPEAKER

    @property
    def default_steps(self) -> str:
        """Diffusion step count used when ``steps`` is omitted.

        Resolved lazily — constructor override, then
        ``STACKCHAN_IRODORI_STEPS``, then
        :data:`DEFAULT_IRODORI_STEPS` — so values loaded from ``.env``
        after import still take effect.
        """
        if self._default_steps_override is not None:
            return self._default_steps_override
        return os.getenv("STACKCHAN_IRODORI_STEPS") or DEFAULT_IRODORI_STEPS

    def _resolve_url(self) -> str:
        """Return the configured endpoint URL or raise a clear error.

        Resolution order: constructor override, then
        ``STACKCHAN_IRODORI_URL``. There is deliberately no fallback
        default — pointing at an unconfigured third-party service would
        be wrong — so an unset URL raises ``RuntimeError`` with setup
        guidance.

        Only surrounding whitespace is stripped; a trailing slash is
        preserved because the value is the endpoint URL itself.
        """
        if self._url_override:
            return self._url_override
        env_url = os.getenv("STACKCHAN_IRODORI_URL")
        if env_url and env_url.strip():
            return env_url.strip()
        raise RuntimeError(
            "Irodori synthesis URL is not configured. Set the "
            "STACKCHAN_IRODORI_URL environment variable to the URL of a "
            "self-hosted Irodori-compatible synthesis API (e.g. a "
            "duplicated Hugging Face Space). The Irodori engine ships no "
            "default endpoint by design."
        )

    def _resolve_api_key(self) -> str | None:
        """Return the API key from the override or the environment."""
        if self._api_key_override is not None:
            return self._api_key_override
        return os.getenv("STACKCHAN_IRODORI_KEY")

    async def synthesize(self, text: str, **opts: Any) -> bytes:
        """Call Irodori, fetch the MP3, return 16 kHz mono PCM.

        Recognised opts:

            ``speaker_id``
                Speaker identifier. Sent verbatim as the ``speaker``
                query parameter; falls back to :attr:`default_speaker`.

            ``steps``
                Diffusion step count. Falls back to :attr:`default_steps`.

            ``seconds``
                Optional target duration; forwarded as the ``seconds``
                query parameter only when provided.

        The text is passed through verbatim, including any emoji —
        Irodori interprets emoji natively as a voice-style cue, so the
        engine must not parse or strip them.
        """
        try:
            import httpx  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - exercised via integration
            raise RuntimeError(
                "httpx is not installed. Install with "
                "'pip install stackchan-mcp[tts-irodori]' to enable Irodori "
                "support."
            ) from exc

        if not isinstance(text, str) or not text.strip():
            raise ValueError("Irodori synthesize: 'text' must be a non-empty string")

        url = self._resolve_url()

        # Speaker / steps are sent verbatim as strings to match the HTTP
        # contract; a caller passing an int is coerced rather than
        # rejected so the say() tool's uniform argument set still works.
        speaker_raw = opts.get("speaker_id")
        speaker = str(speaker_raw) if speaker_raw is not None else self.default_speaker

        steps_raw = opts.get("steps")
        steps = str(steps_raw) if steps_raw is not None else self.default_steps

        params: dict[str, str] = {
            "text": text,
            "speaker": speaker,
            "steps": steps,
        }

        seconds_raw = opts.get("seconds")
        if seconds_raw is not None:
            params["seconds"] = str(seconds_raw)

        api_key = self._resolve_api_key()
        if api_key:
            params["key"] = api_key

        client_kwargs: dict[str, Any] = {"timeout": self._timeout_seconds}
        if self._transport is not None:
            client_kwargs["transport"] = self._transport

        async with httpx.AsyncClient(**client_kwargs) as client:
            # 1. Request synthesis. A non-200 is an engine failure; the
            #    response body may carry a server-provided error message.
            synth_resp = await client.get(url, params=params)
            if synth_resp.status_code != 200:
                raise RuntimeError(
                    f"Irodori synthesis request failed: HTTP "
                    f"{synth_resp.status_code} {synth_resp.text[:200]!r}"
                )

            payload = synth_resp.json()
            if not isinstance(payload, dict):
                raise RuntimeError(
                    f"Irodori returned an unexpected response shape: "
                    f"{type(payload).__name__} (expected a JSON object)"
                )

            if not payload.get("success"):
                server_error = payload.get("error")
                detail = f": {server_error}" if server_error else ""
                raise RuntimeError(f"Irodori synthesis was not successful{detail}")

            # 2. Prefer the streaming URL; fall back to the download URL.
            mp3_url = payload.get("mp3StreamingUrl") or payload.get("mp3DownloadUrl")
            if not mp3_url:
                raise RuntimeError(
                    "Irodori response did not include an MP3 URL "
                    "(neither 'mp3StreamingUrl' nor 'mp3DownloadUrl' was present)."
                )

            # 3. Fetch the MP3 bytes.
            mp3_resp = await client.get(mp3_url)
            if mp3_resp.status_code != 200:
                raise RuntimeError(
                    f"Irodori MP3 fetch failed: HTTP {mp3_resp.status_code} "
                    f"for {mp3_url}"
                )
            mp3_bytes = mp3_resp.content

        if not mp3_bytes:
            raise RuntimeError("Irodori MP3 fetch returned an empty body.")

        # 4. Decode MP3 -> PCM and resample to the device's 16 kHz rate.
        sample_rate, pcm = _decode_mp3_to_pcm16_mono(mp3_bytes)
        if sample_rate != DEVICE_SAMPLE_RATE:
            pcm = resample_pcm16_linear(pcm, sample_rate, DEVICE_SAMPLE_RATE)

        logger.info(
            "Irodori synthesised %d bytes PCM (16 kHz mono) for "
            "speaker=%s, steps=%s, text=%r",
            len(pcm),
            speaker,
            steps,
            text[:60],
        )
        return pcm
