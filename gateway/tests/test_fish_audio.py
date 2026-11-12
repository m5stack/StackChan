"""Tests for the Fish Audio engine HTTP client.

The HTTP layer is exercised with an ``httpx.MockTransport`` (no real
network). Unlike the Irodori tests there is no decode boundary to
monkeypatch: Fish Audio returns 16-bit mono PCM/WAV at a rate we choose,
so the whole audio path here is stdlib-only and runs for real.
"""

from __future__ import annotations

import array
import json

import pytest

httpx = pytest.importorskip("httpx")

from stackchan_mcp.tts.audio_utils import DEVICE_SAMPLE_RATE  # noqa: E402
from stackchan_mcp.tts.fish_audio import (  # noqa: E402
    DEFAULT_FISH_AUDIO_BACKEND,
    DEFAULT_FISH_AUDIO_FORMAT,
    DEFAULT_FISH_AUDIO_URL,
    SUPPORTED_FORMATS,
    FishAudioEngine,
)

from _audio_fixtures import make_wav_bytes  # noqa: E402

_API_KEY = "test-fish-key"
_MODEL_ID = "abc123voicemodel"

#: Every environment variable the engine reads lazily.
_ENV_VARS = (
    "STACKCHAN_FISH_AUDIO_KEY",
    "STACKCHAN_FISH_AUDIO_MODEL",
    "STACKCHAN_FISH_AUDIO_BACKEND",
    "STACKCHAN_FISH_AUDIO_URL",
)


@pytest.fixture(autouse=True)
def _clear_fish_env(monkeypatch):
    """Isolate tests from a developer's real Fish Audio configuration.

    Every setting is resolved from the environment at synthesis time, so
    a stray export would otherwise change what these tests assert.
    """
    for var in _ENV_VARS:
        monkeypatch.delenv(var, raising=False)


def _pcm_16k(n_samples: int = 480) -> bytes:
    """Build ``n_samples`` of signed-16-bit mono PCM at the device rate."""
    return array.array("h", [(i % 100) - 50 for i in range(n_samples)]).tobytes()


def _build_engine(captured: list[dict], *, status: int = 200, body: bytes | None = None,
                  json_body: dict | None = None, headers: dict | None = None, **kwargs):
    """Construct an engine wired to a mock transport recording requests."""
    if body is None and json_body is None:
        body = _pcm_16k()

    def handler(request: httpx.Request) -> httpx.Response:
        captured.append(
            {
                "method": request.method,
                "url": str(request.url),
                "headers": dict(request.headers),
                "json": json.loads(request.content.decode()) if request.content else None,
            }
        )
        if json_body is not None:
            return httpx.Response(status, json=json_body)
        response_headers = headers
        if response_headers is None:
            # A real successful response always declares its type; leaving
            # it out is the malformed case that the engine must reject.
            is_wav = body[:4] == b"RIFF"
            response_headers = {"content-type": "audio/wav" if is_wav else "audio/pcm"}
        return httpx.Response(status, content=body, headers=response_headers)

    kwargs.setdefault("api_key", _API_KEY)
    return FishAudioEngine(transport=httpx.MockTransport(handler), **kwargs)


# ---------------------------------------------------------------------------
# Identity / defaults
# ---------------------------------------------------------------------------


def test_engine_name_is_fish_audio():
    """The registry uses ``name`` to look up engines from the say tool's voice arg."""
    assert FishAudioEngine().name == "fish-audio"


def test_engine_does_not_claim_emoji_style_support():
    """Fish Audio speaks emoji literally, so the orchestrator must strip them."""
    assert FishAudioEngine().supports_emoji_style is False


def test_default_constants_pinned():
    """Defaults pinned so docs stay honest."""
    assert DEFAULT_FISH_AUDIO_URL == "https://api.fish.audio/v1/tts"
    assert DEFAULT_FISH_AUDIO_BACKEND == "s2.1-pro"
    assert DEFAULT_FISH_AUDIO_FORMAT == "pcm"
    assert SUPPORTED_FORMATS == ("pcm", "wav")


def test_engine_is_registered_by_default():
    """The package registers the engine at import time."""
    from stackchan_mcp.tts import get_registry

    assert "fish-audio" in get_registry().names()


# ---------------------------------------------------------------------------
# Lazy configuration resolution
# ---------------------------------------------------------------------------


def test_config_resolves_from_env(monkeypatch):
    """Env vars are read lazily so values from a late-loaded .env still apply."""
    engine = FishAudioEngine()
    monkeypatch.setenv("STACKCHAN_FISH_AUDIO_URL", "https://proxy.test/v1/tts")
    monkeypatch.setenv("STACKCHAN_FISH_AUDIO_MODEL", _MODEL_ID)
    monkeypatch.setenv("STACKCHAN_FISH_AUDIO_BACKEND", "s1")

    assert engine.url == "https://proxy.test/v1/tts"
    assert engine.default_model == _MODEL_ID
    assert engine.backend == "s1"


def test_config_falls_back_to_defaults():
    """With nothing configured, the hosted endpoint and backend defaults apply."""
    engine = FishAudioEngine()
    assert engine.url == DEFAULT_FISH_AUDIO_URL
    assert engine.backend == DEFAULT_FISH_AUDIO_BACKEND
    assert engine.default_model is None


def test_constructor_overrides_beat_env(monkeypatch):
    """An explicit constructor value wins over the environment."""
    monkeypatch.setenv("STACKCHAN_FISH_AUDIO_BACKEND", "s1")
    assert FishAudioEngine(backend="s2-pro").backend == "s2-pro"


@pytest.mark.asyncio
async def test_missing_api_key_raises_actionable_error():
    """An unset key fails with setup guidance, not a bare 401 later."""
    engine = FishAudioEngine()
    with pytest.raises(RuntimeError, match="STACKCHAN_FISH_AUDIO_KEY"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
async def test_api_key_read_from_env(monkeypatch):
    """The key is picked up from the environment at synthesis time."""
    monkeypatch.setenv("STACKCHAN_FISH_AUDIO_KEY", "env-key")
    captured: list[dict] = []

    def handler(request: httpx.Request) -> httpx.Response:
        captured.append(dict(request.headers))
        return httpx.Response(200, content=_pcm_16k(), headers={"content-type": "audio/pcm"})

    engine = FishAudioEngine(transport=httpx.MockTransport(handler))
    await engine.synthesize("hello")

    assert captured[0]["authorization"] == "Bearer env-key"


# ---------------------------------------------------------------------------
# Request building
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_request_shape():
    """POST to the endpoint with auth, backend header, and a 16 kHz ask."""
    captured: list[dict] = []
    engine = _build_engine(captured)

    await engine.synthesize("hello world")

    req = captured[0]
    assert req["method"] == "POST"
    assert req["url"] == DEFAULT_FISH_AUDIO_URL
    assert req["headers"]["authorization"] == f"Bearer {_API_KEY}"
    assert req["headers"]["model"] == DEFAULT_FISH_AUDIO_BACKEND
    assert req["json"]["text"] == "hello world"
    assert req["json"]["format"] == "pcm"
    # Asking for the device's rate is what lets this engine skip resampling.
    assert req["json"]["sample_rate"] == DEVICE_SAMPLE_RATE


@pytest.mark.asyncio
async def test_reference_id_omitted_when_unconfigured():
    """No voice model configured => let Fish Audio use its own default voice."""
    captured: list[dict] = []
    engine = _build_engine(captured)

    await engine.synthesize("hello")

    assert "reference_id" not in captured[0]["json"]


@pytest.mark.asyncio
async def test_default_model_used_as_reference_id():
    captured: list[dict] = []
    engine = _build_engine(captured, default_model=_MODEL_ID)

    await engine.synthesize("hello")

    assert captured[0]["json"]["reference_id"] == _MODEL_ID


@pytest.mark.asyncio
async def test_speaker_name_overrides_default_model():
    """`speaker_name` is the say tool's string speaker selector."""
    captured: list[dict] = []
    engine = _build_engine(captured, default_model=_MODEL_ID)

    await engine.synthesize("hello", speaker_name="per-call-model")

    assert captured[0]["json"]["reference_id"] == "per-call-model"


@pytest.mark.asyncio
async def test_string_speaker_id_used_as_fallback():
    """A string speaker_id still selects a voice, for uniform tool arguments."""
    captured: list[dict] = []
    engine = _build_engine(captured)

    await engine.synthesize("hello", speaker_id="string-model-id")

    assert captured[0]["json"]["reference_id"] == "string-model-id"


@pytest.mark.asyncio
async def test_numeric_speaker_id_is_ignored():
    """Ints are VOICEVOX-style ids; coercing them would forge an invalid model."""
    captured: list[dict] = []
    engine = _build_engine(captured, default_model=_MODEL_ID)

    await engine.synthesize("hello", speaker_id=3)

    assert captured[0]["json"]["reference_id"] == _MODEL_ID


@pytest.mark.asyncio
async def test_optional_tuning_opts_forwarded_only_when_given():
    captured: list[dict] = []
    engine = _build_engine(captured)

    await engine.synthesize("hello", latency="low", temperature=0.3)

    payload = captured[0]["json"]
    assert payload["latency"] == "low"
    assert payload["temperature"] == 0.3
    assert "top_p" not in payload
    assert "normalize" not in payload


@pytest.mark.asyncio
async def test_unknown_opts_are_ignored():
    """The TTSEngine contract: ignore unknown options rather than raise."""
    captured: list[dict] = []
    engine = _build_engine(captured)

    await engine.synthesize("hello", reference_audio="/tmp/x.wav", steps=24)

    assert "reference_audio" not in captured[0]["json"]
    assert "steps" not in captured[0]["json"]


# ---------------------------------------------------------------------------
# Response handling
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_pcm_response_passed_through_unchanged():
    """The happy path needs no decode and no resample."""
    pcm = _pcm_16k()
    engine = _build_engine([], body=pcm)

    assert await engine.synthesize("hello") == pcm


@pytest.mark.asyncio
async def test_odd_length_pcm_is_rejected():
    """A short read is a truncated utterance, not something to repair.

    Trimming the stray byte would realign the framing and return audio
    that is quietly missing its tail, reported as a success. The caller
    cannot tell that from a complete response, so the truncation has to
    surface as an error instead.
    """
    engine = _build_engine([], body=_pcm_16k(10) + b"\x01")

    with pytest.raises(RuntimeError, match="odd PCM byte count"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("content_type", "body"),
    [
        ("text/html; charset=utf-8", b"<html><body>Sign in to continue</body></html>"),
        ("application/json", b'{"error": "quota exceeded"}'),
    ],
)
async def test_non_audio_200_is_rejected(content_type: str, body: bytes):
    """A 200 that is not audio must never reach the speaker.

    This engine carries no decoder -- that is its whole appeal -- so
    nothing downstream would reject a captive portal's login page. It
    would be handed to the device as PCM and played as noise.
    """
    engine = _build_engine([], body=body, headers={"content-type": content_type})

    with pytest.raises(RuntimeError, match="not audio"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
@pytest.mark.parametrize("headers", [{}, {"content-type": ""}])
async def test_missing_or_empty_content_type_is_rejected(headers: dict):
    """A 200 with no Content-Type cannot be confirmed as audio.

    An even-length HTML body would otherwise pass the later framing checks
    and be played as PCM noise.
    """
    engine = _build_engine([], body=b"<html>portal</html>", headers=headers)

    with pytest.raises(RuntimeError, match="without a Content-Type"):
        await engine.synthesize("hello")


@pytest.mark.parametrize("response_format", ["pcm", "wav"])
def test_assert_audio_response_rejects_empty_kind(response_format: str):
    with pytest.raises(RuntimeError, match="without a Content-Type"):
        FishAudioEngine._assert_audio_response("", b"<html>", response_format)


@pytest.mark.asyncio
async def test_successful_responses_declare_audio_content_type():
    """The default mock success responses carry an explicit audio type."""
    seen: list[str] = []

    def handler(request: httpx.Request) -> httpx.Response:
        resp = httpx.Response(200, content=_pcm_16k(), headers={"content-type": "audio/pcm"})
        seen.append(resp.headers["content-type"])
        return resp

    engine = FishAudioEngine(api_key=_API_KEY, transport=httpx.MockTransport(handler))
    assert await engine.synthesize("hello")
    assert seen == ["audio/pcm"]


@pytest.mark.asyncio
async def test_truncated_wav_is_rejected():
    """A RIFF header promising more audio than arrived is a short read.

    ``wave.readframes`` returns what it has rather than raising, so
    without this check a clipped download plays as a clipped utterance
    and still reports success.
    """
    wav = make_wav_bytes(sample_rate=DEVICE_SAMPLE_RATE, samples=[0] * 320)
    engine = _build_engine([], body=wav[: len(wav) - 200], response_format="wav")

    with pytest.raises(RuntimeError, match="truncated WAV"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
async def test_wav_body_when_pcm_was_requested_is_rejected():
    """Its RIFF header would otherwise be played as a burst of noise."""
    wav = make_wav_bytes(sample_rate=DEVICE_SAMPLE_RATE, samples=[0] * 320)
    engine = _build_engine([], body=wav)  # default format is pcm

    with pytest.raises(RuntimeError, match="WAV body despite format='pcm'"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
async def test_wav_response_is_decoded():
    """WAV is parsed with the stdlib reader; no third-party decoder involved."""
    samples = [(i % 50) - 25 for i in range(320)]
    wav = make_wav_bytes(sample_rate=DEVICE_SAMPLE_RATE, samples=samples)
    engine = _build_engine([], body=wav, response_format="wav")

    result = await engine.synthesize("hello")

    assert result == array.array("h", samples).tobytes()


@pytest.mark.asyncio
async def test_wav_response_resampled_when_rate_differs():
    """If the server ignores `sample_rate`, the WAV header drives a resample."""
    wav = make_wav_bytes(sample_rate=32000, duration_ms=100)
    engine = _build_engine([], body=wav, response_format="wav")

    result = await engine.synthesize("hello")

    # 100 ms at 32 kHz (3200 samples) -> 16 kHz (1600 samples, 3200 bytes).
    assert len(result) == 3200


@pytest.mark.asyncio
async def test_unsupported_format_rejected():
    """mp3/opus would need a decoder this engine deliberately omits."""
    engine = _build_engine([], response_format="mp3")

    with pytest.raises(ValueError, match="not supported"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
async def test_empty_body_raises():
    engine = _build_engine([], body=b"")

    with pytest.raises(RuntimeError, match="empty audio body"):
        await engine.synthesize("hello")


# ---------------------------------------------------------------------------
# Error paths
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_empty_text_rejected():
    engine = _build_engine([])

    with pytest.raises(ValueError, match="non-empty string"):
        await engine.synthesize("   ")


@pytest.mark.asyncio
async def test_401_points_at_the_api_key():
    engine = _build_engine([], status=401, json_body={"status": "error", "message": "Invalid token"})

    with pytest.raises(RuntimeError, match="STACKCHAN_FISH_AUDIO_KEY") as exc:
        await engine.synthesize("hello")
    assert "Invalid token" in str(exc.value)


@pytest.mark.asyncio
async def test_402_points_at_billing():
    engine = _build_engine([], status=402, json_body={"status": "error", "message": "No credit"})

    with pytest.raises(RuntimeError, match="insufficient credit") as exc:
        await engine.synthesize("hello")
    assert "No credit" in str(exc.value)


@pytest.mark.asyncio
async def test_other_errors_report_status():
    engine = _build_engine([], status=500, body=b"upstream boom")

    with pytest.raises(RuntimeError, match="HTTP 500"):
        await engine.synthesize("hello")
