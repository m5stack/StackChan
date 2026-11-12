"""Tests for the VOICEVOX engine HTTP client (Issue #70 PR2)."""

from __future__ import annotations

import array

import pytest

httpx = pytest.importorskip("httpx")

from _audio_fixtures import make_wav_bytes  # noqa: E402  (import after importorskip)
from stackchan_mcp.tts.voicevox import (  # noqa: E402
    DEFAULT_VOICEVOX_SPEAKER,
    DEFAULT_VOICEVOX_URL,
    VoicevoxEngine,
)


def _build_handler(captured: list[dict]):
    """Construct an httpx mock handler that emulates the VOICEVOX server."""

    def handler(request: httpx.Request) -> httpx.Response:
        captured.append(
            {
                "method": request.method,
                "path": request.url.path,
                "params": dict(request.url.params),
            }
        )
        path = request.url.path
        if path == "/audio_query":
            return httpx.Response(
                200,
                json={"speedScale": 1.0, "kana": "ハロー", "_test": True},
            )
        if path == "/synthesis":
            wav = make_wav_bytes(
                sample_rate=24000,
                duration_ms=60,
                samples=[i * 10 for i in range(24000 * 60 // 1000)],
            )
            return httpx.Response(200, content=wav)
        return httpx.Response(404)

    return handler


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------


def test_engine_name_is_voicevox():
    """The registry uses ``name`` to look up engines from the say tool's voice arg."""
    engine = VoicevoxEngine()
    assert engine.name == "voicevox"


def test_default_speaker_constant_is_zundamon_normal():
    """3 = Zundamon normal — pinned to keep documentation honest."""
    assert DEFAULT_VOICEVOX_SPEAKER == 3


def test_default_url_constant_matches_docker_image():
    """Default URL must match the upstream voicevox/voicevox_engine port."""
    assert DEFAULT_VOICEVOX_URL == "http://127.0.0.1:50021"


def test_url_param_strips_trailing_slash():
    """A trailing slash on the configured URL would produce ``//audio_query``."""
    engine = VoicevoxEngine(url="http://example.com:50021/")
    assert engine.url == "http://example.com:50021"


def test_default_speaker_param_overrides_env(monkeypatch):
    """Constructor argument wins over the environment variable."""
    monkeypatch.setenv("STACKCHAN_VOICEVOX_DEFAULT_SPEAKER", "8")
    engine = VoicevoxEngine(default_speaker=42)
    assert engine.default_speaker == 42


def test_default_speaker_falls_back_on_invalid_env(monkeypatch):
    """Garbage in the env variable logs a warning and falls back to default."""
    monkeypatch.setenv("STACKCHAN_VOICEVOX_DEFAULT_SPEAKER", "not-an-int")
    engine = VoicevoxEngine()
    assert engine.default_speaker == DEFAULT_VOICEVOX_SPEAKER


# ---------------------------------------------------------------------------
# synthesize() pipeline
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_synthesize_calls_audio_query_then_synthesis():
    """Two HTTP calls in order: /audio_query, then /synthesis."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    engine = VoicevoxEngine(
        url="http://test.local:50021",
        default_speaker=DEFAULT_VOICEVOX_SPEAKER,
        transport=transport,
    )

    pcm = await engine.synthesize("こんにちは")

    assert len(captured) == 2
    assert captured[0]["path"] == "/audio_query"
    assert captured[1]["path"] == "/synthesis"
    assert captured[0]["params"]["text"] == "こんにちは"
    assert captured[0]["params"]["speaker"] == str(DEFAULT_VOICEVOX_SPEAKER)
    assert captured[1]["params"]["speaker"] == str(DEFAULT_VOICEVOX_SPEAKER)
    assert isinstance(pcm, bytes)
    assert len(pcm) > 0


@pytest.mark.asyncio
async def test_synthesize_uses_speaker_id_override():
    """speaker_id in opts overrides the engine's default."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    engine = VoicevoxEngine(
        url="http://test.local:50021",
        default_speaker=3,
        transport=transport,
    )

    await engine.synthesize("hello", speaker_id=14)

    assert captured[0]["params"]["speaker"] == "14"
    assert captured[1]["params"]["speaker"] == "14"


@pytest.mark.asyncio
async def test_synthesize_resamples_24khz_wav_to_16khz():
    """VOICEVOX's 24 kHz output is resampled down to the device's 16 kHz."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    engine = VoicevoxEngine(
        url="http://test.local:50021",
        transport=transport,
    )

    pcm = await engine.synthesize("a" * 10)

    # The handler returns 60 ms @ 24 kHz = 1440 samples = 2880 bytes.
    # After linear resample to 16 kHz: 1440 * 16000 / 24000 = 960 samples
    # = 1920 bytes. Allow a tiny tolerance for the linear interpolator
    # rounding the endpoint mapping.
    decoded = array.array("h")
    decoded.frombytes(pcm)
    assert 950 <= len(decoded) <= 970


@pytest.mark.asyncio
async def test_synthesize_rejects_empty_text():
    """Empty/whitespace text fails fast before any HTTP call."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    engine = VoicevoxEngine(transport=transport)

    with pytest.raises(ValueError, match="text"):
        await engine.synthesize("   ")

    assert captured == []  # never reached the network


@pytest.mark.asyncio
async def test_synthesize_rejects_non_int_speaker_id():
    """Non-integer speaker_id is a clean ValueError, not a TypeError later."""
    transport = httpx.MockTransport(_build_handler([]))
    engine = VoicevoxEngine(transport=transport)

    with pytest.raises(ValueError, match="speaker_id"):
        await engine.synthesize("hello", speaker_id="not-an-int")


@pytest.mark.asyncio
async def test_synthesize_propagates_http_errors():
    """A non-2xx response from VOICEVOX surfaces as an httpx error.

    The orchestrator turns that into a clean MCP error JSON; the
    engine's job is just to fail loudly.
    """

    def handler(request):
        return httpx.Response(503, text="VOICEVOX overloaded")

    transport = httpx.MockTransport(handler)
    engine = VoicevoxEngine(transport=transport)

    with pytest.raises(httpx.HTTPStatusError):
        await engine.synthesize("hello")
