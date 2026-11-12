"""Tests for the ElevenLabs engine HTTP client.

Same approach as the Irodori engine tests: the HTTP layer is exercised
with an ``httpx.MockTransport`` (no real network), and the MP3 decode
boundary (``_decode_mp3_to_pcm16_mono``) is monkeypatched so the tests
cover request building, speaker/voice resolution, and error paths rather
than miniaudio's decoder.
"""

from __future__ import annotations

import array
import json

import pytest

httpx = pytest.importorskip("httpx")

from stackchan_mcp.tts import elevenlabs as eleven_mod  # noqa: E402
from stackchan_mcp.tts.elevenlabs import (  # noqa: E402
    DEFAULT_ELEVEN_MODEL,
    ElevenLabsEngine,
)

_RACHEL_VOICE_ID = "Vid00000000Rachel0000"
_ADAM_VOICE_ID = "Vid00000000Adam000000"

#: A non-empty placeholder standing in for MP3 bytes. The decode boundary
#: is monkeypatched, so the contents never reach a real decoder.
_FAKE_MP3 = b"ID3fake-mp3-bytes"


def _fake_pcm_16k(n_samples: int = 480) -> bytes:
    """Build ``n_samples`` of signed-16-bit mono PCM at the device rate."""
    samples = array.array("h", [(i % 100) - 50 for i in range(n_samples)])
    return samples.tobytes()


@pytest.fixture()
def clean_eleven_env(monkeypatch):
    """Strip every ElevenLabs-related variable from the environment."""
    import os

    for name in list(os.environ):
        if name.startswith("STACKCHAN_ELEVEN") or name == "ELEVENLABS_API_KEY":
            monkeypatch.delenv(name, raising=False)
    return monkeypatch


def _patch_decode(monkeypatch, *, sample_rate: int = 16000, pcm: bytes | None = None):
    """Replace the MP3 decode boundary with a deterministic fake."""
    captured: list[bytes] = []
    payload = pcm if pcm is not None else _fake_pcm_16k()

    def fake_decode(mp3_bytes: bytes):
        captured.append(mp3_bytes)
        return sample_rate, payload

    monkeypatch.setattr(eleven_mod, "_decode_mp3_to_pcm16_mono", fake_decode)
    return captured


def _build_transport(captured: list[httpx.Request], *, status: int = 200, body: bytes = _FAKE_MP3):
    def handler(request: httpx.Request) -> httpx.Response:
        captured.append(request)
        return httpx.Response(status, content=body)

    return httpx.MockTransport(handler)


def _engine(transport: httpx.MockTransport) -> ElevenLabsEngine:
    return ElevenLabsEngine(transport=transport)


def test_engine_declares_no_emoji_style_support():
    assert ElevenLabsEngine().supports_emoji_style is False
    assert ElevenLabsEngine().name == "elevenlabs"


@pytest.mark.asyncio
async def test_synthesize_builds_request_from_named_speaker(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_ADAM", _ADAM_VOICE_ID)
    decode_seen = _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    pcm = await _engine(_build_transport(requests)).synthesize(
        "hello there", speaker_name="rachel"
    )

    assert pcm == _fake_pcm_16k()
    assert decode_seen == [_FAKE_MP3]
    (request,) = requests
    assert request.url.path.endswith(f"/v1/text-to-speech/{_RACHEL_VOICE_ID}")
    assert request.headers["xi-api-key"] == "test-key"
    payload = json.loads(request.content)
    assert payload == {"text": "hello there", "model_id": DEFAULT_ELEVEN_MODEL}


@pytest.mark.asyncio
async def test_sole_configured_voice_is_default(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    await _engine(_build_transport(requests)).synthesize("no speaker given")

    (request,) = requests
    assert request.url.path.endswith(f"/v1/text-to-speech/{_RACHEL_VOICE_ID}")


@pytest.mark.asyncio
async def test_default_speaker_env_selects_voice(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_ADAM", _ADAM_VOICE_ID)
    monkeypatch.setenv("STACKCHAN_ELEVEN_DEFAULT_SPEAKER", "adam")
    _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    await _engine(_build_transport(requests)).synthesize("no speaker given")

    (request,) = requests
    assert request.url.path.endswith(f"/v1/text-to-speech/{_ADAM_VOICE_ID}")


@pytest.mark.asyncio
async def test_raw_voice_id_passes_through(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    await _engine(_build_transport(requests)).synthesize(
        "raw id", speaker_name="Vid0000000000RawId000"
    )

    (request,) = requests
    assert request.url.path.endswith("/v1/text-to-speech/Vid0000000000RawId000")


@pytest.mark.asyncio
async def test_unknown_speaker_falls_back_to_default(clean_eleven_env, caplog):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    await _engine(_build_transport(requests)).synthesize("hi", speaker_name="nobody")

    (request,) = requests
    assert request.url.path.endswith(f"/v1/text-to-speech/{_RACHEL_VOICE_ID}")


@pytest.mark.asyncio
async def test_no_voice_configured_raises(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    _patch_decode(monkeypatch)

    with pytest.raises(RuntimeError, match="not configured"):
        await _engine(_build_transport([])).synthesize("hi", speaker_name="nobody")


@pytest.mark.asyncio
async def test_missing_api_key_raises(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)

    with pytest.raises(RuntimeError, match="API key"):
        await _engine(_build_transport([])).synthesize("hi")


@pytest.mark.asyncio
async def test_empty_text_raises(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)

    with pytest.raises(ValueError, match="non-empty"):
        await _engine(_build_transport([])).synthesize("   ")


@pytest.mark.asyncio
async def test_http_error_raises(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)

    with pytest.raises(RuntimeError, match="HTTP 401"):
        await _engine(_build_transport([], status=401, body=b"denied")).synthesize("hi")


@pytest.mark.asyncio
async def test_empty_audio_body_raises(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)

    with pytest.raises(RuntimeError, match="empty audio"):
        await _engine(_build_transport([], body=b"")).synthesize("hi")


@pytest.mark.asyncio
async def test_non_device_sample_rate_is_resampled(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch, sample_rate=32000, pcm=_fake_pcm_16k(640))

    pcm = await _engine(_build_transport([])).synthesize("hi")

    # 640 samples at 32 kHz resample to ~320 samples at 16 kHz (2 bytes each).
    assert abs(len(pcm) - 320 * 2) <= 4


@pytest.mark.asyncio
async def test_stackchan_key_takes_precedence_over_generic_key(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "generic-key")
    monkeypatch.setenv("STACKCHAN_ELEVENLABS_KEY", "specific-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    await _engine(_build_transport(requests)).synthesize("hi")

    (request,) = requests
    assert request.headers["xi-api-key"] == "specific-key"


@pytest.mark.asyncio
async def test_model_env_overrides_default(clean_eleven_env):
    monkeypatch = clean_eleven_env
    monkeypatch.setenv("ELEVENLABS_API_KEY", "test-key")
    monkeypatch.setenv("STACKCHAN_ELEVEN_VOICE_RACHEL", _RACHEL_VOICE_ID)
    monkeypatch.setenv("STACKCHAN_ELEVEN_MODEL", "eleven_turbo_v2_5")
    _patch_decode(monkeypatch)
    requests: list[httpx.Request] = []

    await _engine(_build_transport(requests)).synthesize("hi")

    (request,) = requests
    assert json.loads(request.content)["model_id"] == "eleven_turbo_v2_5"


def test_engine_is_registered():
    from stackchan_mcp.tts import get_registry

    assert "elevenlabs" in get_registry().names()
