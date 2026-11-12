"""Fish Audio engine — HTTP client for the hosted Fish Audio TTS API.

Fish Audio (https://fish.audio) is a hosted text-to-speech service with
strong English voices and user-trainable voice models, which makes it a
practical alternative to VOICEVOX (Japanese-only by default) for English
deployments.

Unlike the Irodori and Edge TTS engines, this one needs **no audio
decoder**: the Fish Audio API can return 16-bit mono PCM/WAV at a caller-
chosen sample rate, and 16 kHz is one of the supported rates — exactly
what the device's Opus decoder expects. The engine therefore requests
audio at :data:`~stackchan_mcp.tts.audio_utils.DEVICE_SAMPLE_RATE`
directly and hands the PCM to the orchestrator without resampling in the
common case. That keeps this engine on the base ``[tts]`` extra (httpx +
opuslib) with no extra dependency of its own — no ``miniaudio`` (Irodori)
and no ``ffmpeg`` (Edge TTS).

``mp3`` and ``opus`` response formats are deliberately *not* supported:
consuming them would drag in a decoder purely to undo an encode we never
needed.

API contract (https://docs.fish.audio, ``POST /v1/tts``)::

    POST https://api.fish.audio/v1/tts
    Authorization: Bearer <api key>
    Content-Type: application/json
    model: <backend>            # s1 | s2-pro | s2.1-pro | s2.1-pro-free

    {
        "text": "...",
        "reference_id": "<voice model id>",   # optional
        "format": "pcm" | "wav",
        "sample_rate": 16000,
        "normalize": true,
        "latency": "normal" | "balanced" | "low"
    }

    -> 200  raw audio bytes (Transfer-Encoding: chunked)
       401  JSON {"status", "message"}  — bad / missing API key
       402  JSON {"status", "message"}  — out of credit

Configuration (environment variables):

    ``STACKCHAN_FISH_AUDIO_KEY``
        API key, sent as ``Authorization: Bearer <key>``. **Required** —
        synthesis fails with a clear error when unset. Read from the
        environment only; never commit it.

    ``STACKCHAN_FISH_AUDIO_MODEL``
        Default voice model ID (the API's ``reference_id``), i.e. the
        voice to speak in. Optional: when unset the request omits
        ``reference_id`` and Fish Audio uses its own default voice.

    ``STACKCHAN_FISH_AUDIO_BACKEND``
        Synthesis backend, sent as the ``model`` HTTP header. Default
        :data:`DEFAULT_FISH_AUDIO_BACKEND`.

    ``STACKCHAN_FISH_AUDIO_URL``
        Endpoint override. Default :data:`DEFAULT_FISH_AUDIO_URL`. Useful
        for a proxy or a self-hosted compatible deployment.
"""

from __future__ import annotations

import io
import logging
import os
import wave
from typing import Any

from .audio_utils import (
    DEVICE_SAMPLE_RATE,
    resample_pcm16_linear,
    wav_to_pcm16_mono,
)
from .base import TTSEngine

logger = logging.getLogger(__name__)


#: Default synthesis endpoint. Unlike Irodori (a service each user
#: self-hosts), Fish Audio is a single hosted API with a stable public
#: URL, so shipping a default here points everyone at the right place
#: rather than at someone's private deployment.
DEFAULT_FISH_AUDIO_URL = "https://api.fish.audio/v1/tts"

#: Default synthesis backend, sent as the ``model`` HTTP header. Pinned
#: to the API's documented default rather than left unset so behaviour
#: stays reproducible if the service changes its own default. Valid
#: values: ``s1``, ``s2-pro``, ``s2.1-pro``, ``s2.1-pro-free``.
DEFAULT_FISH_AUDIO_BACKEND = "s2.1-pro"

#: Response formats this engine can consume. Both are 16-bit mono at the
#: requested sample rate; ``pcm`` is headerless (rate is whatever we
#: asked for) while ``wav`` self-describes its rate in the RIFF header.
#: ``pcm`` is the default because a chunked/streamed WAV can carry a
#: placeholder length in its header, which the stdlib ``wave`` reader
#: would honour and truncate on.
SUPPORTED_FORMATS = ("pcm", "wav")

#: Default response format. See :data:`SUPPORTED_FORMATS`.
DEFAULT_FISH_AUDIO_FORMAT = "pcm"

#: HTTP timeout. More generous than the Irodori engine's 30 s: Fish Audio
#: synthesis is an LLM-style generation whose latency scales with text
#: length, and a cold model can add several seconds on the first call.
DEFAULT_HTTP_TIMEOUT_SECONDS = 60.0

#: Content types a 200 may carry and still be audio. Fish Audio labels
#: its bodies ``audio/*``; ``application/octet-stream`` is allowed
#: because headerless ``pcm`` has no type of its own and proxies
#: commonly relabel it. Everything else on a 200 -- ``text/html`` from a
#: captive portal, ``application/json`` from an error-shaped success --
#: is the failure this check exists to catch.
_AUDIO_CONTENT_TYPES = ("audio/", "application/octet-stream", "binary/octet-stream")


class FishAudioEngine(TTSEngine):
    """Synthesise text via the hosted Fish Audio TTS API.

    Setup: create an API key at https://fish.audio, export it as
    ``STACKCHAN_FISH_AUDIO_KEY``, and (optionally) set
    ``STACKCHAN_FISH_AUDIO_MODEL`` to the ID of the voice model you want
    to speak in. Only the base ``[tts]`` extra is needed:
    ``pip install stackchan-mcp[tts]``.

    Emoji are *not* forwarded: Fish Audio reads them as literal text
    rather than as voice-style cues, so :attr:`supports_emoji_style`
    stays false and the orchestrator hands this engine emoji-stripped
    text.
    """

    name = "fish-audio"

    def __init__(
        self,
        url: str | None = None,
        *,
        api_key: str | None = None,
        default_model: str | None = None,
        backend: str | None = None,
        response_format: str | None = None,
        timeout_seconds: float = DEFAULT_HTTP_TIMEOUT_SECONDS,
        transport: Any = None,
    ) -> None:
        """Construct a Fish Audio engine.

        Args:
            url: Endpoint override. When ``None`` the
                ``STACKCHAN_FISH_AUDIO_URL`` environment variable is read
                at synthesis time, falling back to
                :data:`DEFAULT_FISH_AUDIO_URL`.
            api_key: API key. When ``None`` the
                ``STACKCHAN_FISH_AUDIO_KEY`` environment variable is read
                at synthesis time.
            default_model: Voice model ID (``reference_id``) used when a
                call omits ``speaker_name``. Falls back to
                ``STACKCHAN_FISH_AUDIO_MODEL``, then to omitting the
                field entirely (Fish Audio's own default voice).
            backend: Synthesis backend for the ``model`` header. Falls
                back to ``STACKCHAN_FISH_AUDIO_BACKEND`` then
                :data:`DEFAULT_FISH_AUDIO_BACKEND`.
            response_format: ``"pcm"`` or ``"wav"``. Falls back to
                :data:`DEFAULT_FISH_AUDIO_FORMAT`.
            timeout_seconds: HTTP timeout for the synthesis request.
            transport: An :class:`httpx.BaseTransport` (or compatible)
                handed straight to :class:`httpx.AsyncClient`. Tests pass
                a :class:`httpx.MockTransport` to avoid hitting the
                network; production callers leave it ``None``.

        Every environment-backed setting is resolved lazily (at synthesis
        time) rather than captured here, for the same reason as the
        Irodori engine: under ``serve --transport streamable-http`` the
        engine is constructed at import time, before ``.env`` is loaded,
        so reading the environment in ``__init__`` would silently ignore
        dotenv-provided values. Only explicit constructor overrides are
        pinned at construction time.
        """
        self._url_override = url.strip() if url and url.strip() else None
        self._api_key_override = api_key
        self._default_model_override = default_model
        self._backend_override = backend
        self._response_format_override = response_format
        self._timeout_seconds = timeout_seconds
        self._transport = transport

    # -- lazily resolved configuration ---------------------------------

    @property
    def url(self) -> str:
        """Synthesis endpoint: override, then env, then the default."""
        if self._url_override:
            return self._url_override
        env_url = os.getenv("STACKCHAN_FISH_AUDIO_URL")
        if env_url and env_url.strip():
            return env_url.strip()
        return DEFAULT_FISH_AUDIO_URL

    @property
    def default_model(self) -> str | None:
        """Voice model ID used when ``speaker_name`` is omitted.

        ``None`` means "send no ``reference_id``", which makes Fish Audio
        fall back to its own default voice.
        """
        if self._default_model_override is not None:
            return self._default_model_override or None
        env_model = os.getenv("STACKCHAN_FISH_AUDIO_MODEL")
        return env_model.strip() if env_model and env_model.strip() else None

    @property
    def backend(self) -> str:
        """Synthesis backend sent as the ``model`` header."""
        if self._backend_override is not None:
            return self._backend_override
        env_backend = os.getenv("STACKCHAN_FISH_AUDIO_BACKEND")
        if env_backend and env_backend.strip():
            return env_backend.strip()
        return DEFAULT_FISH_AUDIO_BACKEND

    @property
    def response_format(self) -> str:
        """Requested response format (``"pcm"`` or ``"wav"``)."""
        if self._response_format_override is not None:
            return self._response_format_override
        return DEFAULT_FISH_AUDIO_FORMAT

    def _resolve_api_key(self) -> str:
        """Return the API key or raise a clear, actionable error."""
        key = self._api_key_override
        if key is None:
            key = os.getenv("STACKCHAN_FISH_AUDIO_KEY")
        if key and key.strip():
            return key.strip()
        raise RuntimeError(
            "Fish Audio API key is not configured. Set the "
            "STACKCHAN_FISH_AUDIO_KEY environment variable to a key from "
            "https://fish.audio (Settings -> API Keys)."
        )

    # -- synthesis ------------------------------------------------------

    async def synthesize(self, text: str, **opts: Any) -> bytes:
        """Call Fish Audio and return 16 kHz mono PCM (signed 16-bit LE).

        Recognised opts:

            ``speaker_name``
                Voice model ID (the API's ``reference_id``). This is the
                string speaker selector, matching the ``say`` tool's
                ``speaker_name`` argument. Falls back to
                :attr:`default_model`.

            ``speaker_id``
                Accepted only for uniformity with the numeric-speaker
                engines. Used as the voice model ID when it is a
                non-empty string and ``speaker_name`` was not given;
                otherwise ignored (Fish Audio voice models are string
                IDs, not numbers).

            ``latency``
                ``"normal"``, ``"balanced"`` or ``"low"``. Forwarded only
                when provided.

            ``temperature`` / ``top_p``
                Sampling controls, forwarded only when provided.

            ``normalize``
                Whether Fish Audio should normalise the input text.
                Forwarded only when provided.

        Unknown options are ignored, per the :class:`TTSEngine` contract.
        """
        try:
            import httpx  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - exercised via integration
            raise RuntimeError(
                "httpx is not installed. Install with "
                "'pip install stackchan-mcp[tts]' to enable Fish Audio support."
            ) from exc

        if not isinstance(text, str) or not text.strip():
            raise ValueError("Fish Audio synthesize: 'text' must be a non-empty string")

        response_format = self.response_format
        if response_format not in SUPPORTED_FORMATS:
            raise ValueError(
                f"Fish Audio response format {response_format!r} is not supported. "
                f"Use one of {list(SUPPORTED_FORMATS)}: the compressed formats "
                "(mp3, opus) would require an audio decoder this engine "
                "deliberately does not carry."
            )

        api_key = self._resolve_api_key()

        # Voice model: speaker_name wins, then a string speaker_id, then
        # the configured default. None => omit the field entirely.
        reference_id = self._resolve_reference_id(opts)

        payload: dict[str, Any] = {
            "text": text,
            "format": response_format,
            # Ask for the device's rate directly so the common path needs
            # no resampling. Both supported formats honour this field.
            "sample_rate": DEVICE_SAMPLE_RATE,
        }
        if reference_id:
            payload["reference_id"] = reference_id

        for key in ("latency", "temperature", "top_p", "normalize"):
            if opts.get(key) is not None:
                payload[key] = opts[key]

        headers = {
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            # Selects the synthesis backend; see DEFAULT_FISH_AUDIO_BACKEND.
            "model": self.backend,
        }

        client_kwargs: dict[str, Any] = {"timeout": self._timeout_seconds}
        if self._transport is not None:
            client_kwargs["transport"] = self._transport

        async with httpx.AsyncClient(**client_kwargs) as client:
            resp = await client.post(self.url, json=payload, headers=headers)
            if resp.status_code != 200:
                raise RuntimeError(self._describe_error(resp))
            content_type = resp.headers.get("content-type", "")
            audio = resp.content

        if not audio:
            raise RuntimeError(
                "Fish Audio returned an empty audio body. The request was "
                "accepted but produced no audio — check that the voice model "
                f"ID ({reference_id or 'default voice'}) is valid."
            )

        self._assert_audio_response(content_type, audio, response_format)

        pcm, sample_rate = self._to_pcm16_mono(audio, response_format)

        # For "pcm" the rate is whatever we asked for, so this is a no-op;
        # for "wav" it corrects a server that ignored `sample_rate`.
        if sample_rate != DEVICE_SAMPLE_RATE:
            pcm = resample_pcm16_linear(pcm, sample_rate, DEVICE_SAMPLE_RATE)

        logger.info(
            "Fish Audio synthesised %d bytes PCM (16 kHz mono) for "
            "model=%s, backend=%s, format=%s, text=%r",
            len(pcm),
            reference_id or "(default voice)",
            self.backend,
            response_format,
            text[:60],
        )
        return pcm

    # -- helpers --------------------------------------------------------

    def _resolve_reference_id(self, opts: dict[str, Any]) -> str | None:
        """Pick the voice model ID from opts, else the configured default."""
        speaker_name = opts.get("speaker_name")
        if isinstance(speaker_name, str) and speaker_name.strip():
            return speaker_name.strip()

        # Fish Audio voice models are string IDs. A caller passing the
        # numeric-speaker argument still gets a sensible result when the
        # value happens to be a string; genuine ints are ignored rather
        # than coerced into a guaranteed-invalid model ID.
        speaker_id = opts.get("speaker_id")
        if isinstance(speaker_id, str) and speaker_id.strip():
            return speaker_id.strip()

        return self.default_model

    @staticmethod
    def _assert_audio_response(
        content_type: str, audio: bytes, response_format: str
    ) -> None:
        """Reject a 200 whose body is not the audio we asked for.

        Every other engine gets this for free: VOICEVOX and Irodori push
        their responses through a decoder, which fails on anything that
        is not audio. The appeal of this engine is that it carries no
        decoder, and that is exactly what removes the layer implicitly
        validating the body -- so the check has to be explicit here.

        Without it a captive portal or a proxy answering 200 with HTML or
        JSON is handed to the device as PCM and played as noise, which is
        both alarming and hard to trace back to a network problem.
        """
        kind = content_type.split(";", 1)[0].strip().lower()
        if not kind:
            raise RuntimeError(
                "Fish Audio returned HTTP 200 without a Content-Type, so the "
                "body cannot be confirmed as audio. A proxy or captive portal "
                "most likely answered instead of the API; the body was not played."
            )
        if not kind.startswith(_AUDIO_CONTENT_TYPES):
            raise RuntimeError(
                f"Fish Audio returned HTTP 200 with Content-Type {kind!r}, "
                "which is not audio. A proxy or captive portal most likely "
                "answered instead of the API; the body was not played."
            )

        # The declared format has to match what was asked for: a WAV body
        # handed back as headerless `pcm` would play its own RIFF header
        # as several milliseconds of noise.
        looks_like_wav = audio[:4] == b"RIFF" and audio[8:12] == b"WAVE"
        if response_format == "wav" and not looks_like_wav:
            raise RuntimeError(
                "Fish Audio returned a body that is not RIFF/WAVE despite "
                "format='wav' being requested; the body was not played."
            )
        if response_format == "pcm" and looks_like_wav:
            raise RuntimeError(
                "Fish Audio returned a WAV body despite format='pcm' being "
                "requested; its header would be played as noise."
            )

    @staticmethod
    def _to_pcm16_mono(audio: bytes, response_format: str) -> tuple[bytes, int]:
        """Normalise a response body to ``(pcm, sample_rate)``.

        ``wav`` is parsed with the stdlib reader (which also downmixes
        stereo and reports the true rate), then checked against the frame
        count its own header declares. ``pcm`` is already the target
        representation and only has to be a whole number of samples.

        Both checks reject rather than repair. A response that is short
        of what it promised is a truncated utterance, and playing it
        while reporting success turns a network fault into a wrong answer
        the caller cannot see.
        """
        if response_format == "wav":
            with wave.open(io.BytesIO(audio), "rb") as wav:
                declared = (
                    wav.getnframes() * wav.getnchannels() * wav.getsampwidth()
                )
                actual = len(wav.readframes(wav.getnframes()))
            if actual < declared:
                raise RuntimeError(
                    "Fish Audio returned a truncated WAV: its header declares "
                    f"{declared} bytes of audio but only {actual} arrived. "
                    "The response was cut short in transit."
                )
            sample_rate, pcm = wav_to_pcm16_mono(audio)
            return pcm, sample_rate

        if len(audio) % 2:
            raise RuntimeError(
                f"Fish Audio returned an odd PCM byte count ({len(audio)}), "
                "which cannot be a whole number of 16-bit samples. The "
                "response was truncated in transit."
            )
        return audio, DEVICE_SAMPLE_RATE

    @staticmethod
    def _describe_error(resp: Any) -> str:
        """Build an actionable message for a non-200 response.

        401 and 402 are the two failures users actually hit (bad key, no
        credit), and the API documents a JSON ``message`` for both, so
        they get specific guidance instead of a raw status dump.
        """
        detail = ""
        try:
            body = resp.json()
            if isinstance(body, dict) and body.get("message"):
                detail = f": {body['message']}"
        except Exception:  # noqa: BLE001 - body may be non-JSON audio/HTML
            snippet = resp.text[:200]
            if snippet:
                detail = f": {snippet!r}"

        if resp.status_code == 401:
            return (
                f"Fish Audio rejected the API key (HTTP 401){detail}. Check "
                "STACKCHAN_FISH_AUDIO_KEY against https://fish.audio "
                "(Settings -> API Keys)."
            )
        if resp.status_code == 402:
            return (
                f"Fish Audio reported insufficient credit (HTTP 402){detail}. "
                "Top up the account at https://fish.audio."
            )
        return f"Fish Audio synthesis failed: HTTP {resp.status_code}{detail}"
