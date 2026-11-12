"""Tests for the Irodori engine HTTP client (Issue #286).

The HTTP layer is exercised with an ``httpx.MockTransport`` (no real
network). MP3 decoding is the one piece that would otherwise need the
``miniaudio`` C extension from the ``[tts-irodori]`` extra, so the decode
boundary (``_decode_mp3_to_pcm16_mono``) is monkeypatched: these tests
verify the engine's request building, response handling, URL-selection,
and error paths, not miniaudio's decoder.
"""

from __future__ import annotations

import array

import pytest

httpx = pytest.importorskip("httpx")

from stackchan_mcp.tts import irodori as irodori_mod  # noqa: E402
from stackchan_mcp.tts.irodori import (  # noqa: E402
    DEFAULT_IRODORI_SPEAKER,
    DEFAULT_IRODORI_STEPS,
    IrodoriEngine,
)

_SYNTH_URL = "http://irodori.test/api/synthesize"
_MP3_STREAM_URL = "http://irodori.test/files/out.mp3?stream=1"
_MP3_DOWNLOAD_URL = "http://irodori.test/files/out.mp3"

#: A non-empty placeholder standing in for MP3 bytes. The decode boundary
#: is monkeypatched, so the actual contents never reach a real decoder.
_FAKE_MP3 = b"ID3fake-mp3-bytes"


def _fake_pcm_16k(n_samples: int = 480) -> bytes:
    """Build ``n_samples`` of signed-16-bit mono PCM at the device rate."""
    samples = array.array("h", [(i % 100) - 50 for i in range(n_samples)])
    return samples.tobytes()


def test_irodori_engine_declares_emoji_style_support():
    engine = IrodoriEngine(url=_SYNTH_URL)
    assert engine.supports_emoji_style is True


def _patch_decode(monkeypatch, *, sample_rate: int = 16000, pcm: bytes | None = None):
    """Replace the MP3 decode boundary with a deterministic fake.

    Returns a one-element list capturing the bytes handed to the decoder
    so callers can assert the engine fetched the right MP3.
    """
    captured: list[bytes] = []
    payload = pcm if pcm is not None else _fake_pcm_16k()

    def fake_decode(mp3_bytes: bytes):
        captured.append(mp3_bytes)
        return sample_rate, payload

    monkeypatch.setattr(irodori_mod, "_decode_mp3_to_pcm16_mono", fake_decode)
    return captured


def _build_handler(
    captured: list[dict],
    *,
    synth_status: int = 200,
    synth_json: dict | None = None,
    mp3_status: int = 200,
    mp3_body: bytes = _FAKE_MP3,
):
    """Construct an httpx mock handler emulating the Irodori service.

    The synthesis endpoint (``_SYNTH_URL``) returns ``synth_json``; any
    other GET is treated as the MP3 fetch and returns ``mp3_body``.
    """
    if synth_json is None:
        synth_json = {
            "success": True,
            "mp3StreamingUrl": _MP3_STREAM_URL,
            "mp3DownloadUrl": _MP3_DOWNLOAD_URL,
        }

    def handler(request: httpx.Request) -> httpx.Response:
        url_no_query = str(request.url).split("?", 1)[0]
        captured.append(
            {
                "method": request.method,
                "url": url_no_query,
                "params": dict(request.url.params),
            }
        )
        if url_no_query == _SYNTH_URL:
            return httpx.Response(synth_status, json=synth_json)
        # Anything else is the MP3 fetch.
        return httpx.Response(mp3_status, content=mp3_body)

    return handler


# ---------------------------------------------------------------------------
# Defaults / construction
# ---------------------------------------------------------------------------


def test_engine_name_is_irodori():
    """The registry uses ``name`` to look up engines from the say tool's voice arg."""
    engine = IrodoriEngine(url=_SYNTH_URL)
    assert engine.name == "irodori"


def test_default_speaker_and_steps_constants():
    """Defaults pinned so docs stay honest."""
    assert DEFAULT_IRODORI_SPEAKER == "3"
    assert DEFAULT_IRODORI_STEPS == "24"


def test_defaults_read_from_env(monkeypatch):
    """Speaker / steps fall back to env vars when not passed to the constructor."""
    monkeypatch.setenv("STACKCHAN_IRODORI_SPEAKER", "7")
    monkeypatch.setenv("STACKCHAN_IRODORI_STEPS", "32")
    engine = IrodoriEngine(url=_SYNTH_URL)
    assert engine.default_speaker == "7"
    assert engine.default_steps == "32"


def test_constructor_args_override_env(monkeypatch):
    """Constructor arguments win over the environment."""
    monkeypatch.setenv("STACKCHAN_IRODORI_SPEAKER", "7")
    monkeypatch.setenv("STACKCHAN_IRODORI_STEPS", "32")
    engine = IrodoriEngine(url=_SYNTH_URL, default_speaker="2", default_steps="16")
    assert engine.default_speaker == "2"
    assert engine.default_steps == "16"


def test_defaults_resolve_lazily_after_construction(monkeypatch):
    """Env defaults set after construction still take effect.

    With ``serve --transport streamable-http`` the tts package is
    imported (and the engine registered) before ``.env`` is loaded, so
    speaker / steps must be read at use time, not captured at
    construction time.
    """
    monkeypatch.delenv("STACKCHAN_IRODORI_SPEAKER", raising=False)
    monkeypatch.delenv("STACKCHAN_IRODORI_STEPS", raising=False)
    engine = IrodoriEngine(url=_SYNTH_URL)  # constructed before env is set
    monkeypatch.setenv("STACKCHAN_IRODORI_SPEAKER", "7")
    monkeypatch.setenv("STACKCHAN_IRODORI_STEPS", "48")
    assert engine.default_speaker == "7"
    assert engine.default_steps == "48"


# ---------------------------------------------------------------------------
# Unset URL — registers but fails clearly on synthesize
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_synthesize_without_url_raises_clear_error(monkeypatch):
    """No STACKCHAN_IRODORI_URL -> RuntimeError naming the env var.

    The engine still constructs (so it stays discoverable in the
    registry); the missing-URL condition surfaces only when synthesis is
    attempted.
    """
    monkeypatch.delenv("STACKCHAN_IRODORI_URL", raising=False)
    _patch_decode(monkeypatch)
    engine = IrodoriEngine()  # no url, no env

    with pytest.raises(RuntimeError, match="STACKCHAN_IRODORI_URL"):
        await engine.synthesize("hello")


@pytest.mark.asyncio
async def test_synthesize_reads_url_from_env(monkeypatch):
    """When no url is passed, STACKCHAN_IRODORI_URL is used."""
    monkeypatch.setenv("STACKCHAN_IRODORI_URL", _SYNTH_URL)
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(transport=transport)

    pcm = await engine.synthesize("hello")

    assert isinstance(pcm, bytes)
    assert len(pcm) > 0
    assert captured[0]["url"] == _SYNTH_URL


@pytest.mark.asyncio
async def test_synthesize_preserves_trailing_slash_url(monkeypatch):
    """A trailing slash on the endpoint URL is requested verbatim.

    The configured value is the synthesis *endpoint* URL, not a base
    URL: a deployment defined as ``.../api/synthesize/`` must not be
    rewritten to ``.../api/synthesize``, which strict-routing servers
    treat as a different path.
    """
    slash_url = _SYNTH_URL + "/"
    requests: list[str] = []

    def handler(request: httpx.Request) -> httpx.Response:
        url_no_query = str(request.url).split("?", 1)[0]
        requests.append(url_no_query)
        if url_no_query == slash_url:
            return httpx.Response(
                200,
                json={"success": True, "mp3StreamingUrl": _MP3_STREAM_URL},
            )
        return httpx.Response(200, content=_FAKE_MP3)

    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=slash_url, transport=httpx.MockTransport(handler))

    pcm = await engine.synthesize("hello")

    assert requests[0] == slash_url
    assert isinstance(pcm, bytes)
    assert len(pcm) > 0


def test_resolve_url_env_trailing_slash_and_whitespace(monkeypatch):
    """Env URL keeps its trailing slash; only surrounding whitespace goes."""
    slash_url = _SYNTH_URL + "/"
    monkeypatch.setenv("STACKCHAN_IRODORI_URL", f"  {slash_url}  ")
    engine = IrodoriEngine()

    assert engine._resolve_url() == slash_url


# ---------------------------------------------------------------------------
# Happy path
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_synthesize_happy_path_get_json_fetch_pcm(monkeypatch):
    """GET synth -> JSON -> fetch streaming MP3 -> decoded PCM bytes."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    decode_captured = _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    pcm = await engine.synthesize("こんにちは")

    # Two GETs: the synthesis request then the MP3 fetch.
    assert len(captured) == 2
    assert captured[0]["method"] == "GET"
    assert captured[0]["url"] == _SYNTH_URL
    assert captured[0]["params"]["text"] == "こんにちは"
    assert captured[0]["params"]["speaker"] == DEFAULT_IRODORI_SPEAKER
    assert captured[0]["params"]["steps"] == DEFAULT_IRODORI_STEPS
    # Streaming URL is preferred over the download URL.
    assert captured[1]["url"] == _MP3_STREAM_URL.split("?", 1)[0]
    # The fetched MP3 bytes were the ones handed to the decoder.
    assert decode_captured == [_FAKE_MP3]
    assert isinstance(pcm, bytes)
    assert len(pcm) > 0


@pytest.mark.asyncio
async def test_synthesize_passes_speaker_and_steps_overrides(monkeypatch):
    """speaker_id / steps opts are forwarded as query params verbatim."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    await engine.synthesize("hi", speaker_id=9, steps=40, seconds=5)

    params = captured[0]["params"]
    assert params["speaker"] == "9"
    assert params["steps"] == "40"
    assert params["seconds"] == "5"


@pytest.mark.asyncio
async def test_synthesize_forwards_api_key_when_set(monkeypatch):
    """An API key (env) is forwarded as the 'key' query param."""
    monkeypatch.setenv("STACKCHAN_IRODORI_KEY", "secret-token")
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    await engine.synthesize("hi")

    assert captured[0]["params"]["key"] == "secret-token"


@pytest.mark.asyncio
async def test_synthesize_omits_key_when_unset(monkeypatch):
    """No API key -> no 'key' query param (not an empty one)."""
    monkeypatch.delenv("STACKCHAN_IRODORI_KEY", raising=False)
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    await engine.synthesize("hi")

    assert "key" not in captured[0]["params"]


@pytest.mark.asyncio
async def test_synthesize_passes_emoji_through_verbatim(monkeypatch):
    """Emoji in the text must reach the API unmodified (style cue)."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    await engine.synthesize("やったね😊🎉")

    assert captured[0]["params"]["text"] == "やったね😊🎉"


@pytest.mark.asyncio
async def test_synthesize_falls_back_to_download_url(monkeypatch):
    """When only mp3DownloadUrl is present, it is used."""
    captured: list[dict] = []
    handler = _build_handler(
        captured,
        synth_json={
            "success": True,
            "mp3StreamingUrl": None,
            "mp3DownloadUrl": _MP3_DOWNLOAD_URL,
        },
    )
    transport = httpx.MockTransport(handler)
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    await engine.synthesize("hi")

    assert captured[1]["url"] == _MP3_DOWNLOAD_URL.split("?", 1)[0]


@pytest.mark.asyncio
async def test_synthesize_resamples_non_device_rate(monkeypatch):
    """A 48 kHz decode is resampled down to the device's 16 kHz."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    # 48 kHz, 1440 samples (30 ms). Resampled to 16 kHz -> ~480 samples.
    _patch_decode(monkeypatch, sample_rate=48000, pcm=_fake_pcm_16k(1440))
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    pcm = await engine.synthesize("hi")

    decoded = array.array("h")
    decoded.frombytes(pcm)
    # 1440 * 16000 / 48000 = 480, allow tiny interpolation tolerance.
    assert 470 <= len(decoded) <= 490


# ---------------------------------------------------------------------------
# Error paths
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_synthesize_rejects_empty_text(monkeypatch):
    """Empty/whitespace text fails fast before any HTTP call."""
    captured: list[dict] = []
    transport = httpx.MockTransport(_build_handler(captured))
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    with pytest.raises(ValueError, match="text"):
        await engine.synthesize("   ")

    assert captured == []  # never reached the network


@pytest.mark.asyncio
async def test_synthesize_success_false_raises_with_server_error(monkeypatch):
    """success: false surfaces the server-provided error text."""
    captured: list[dict] = []
    handler = _build_handler(
        captured,
        synth_json={"success": False, "error": "speaker out of range"},
    )
    transport = httpx.MockTransport(handler)
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    with pytest.raises(RuntimeError, match="speaker out of range"):
        await engine.synthesize("hi")

    # Only the synthesis request happened; no MP3 fetch.
    assert len(captured) == 1


@pytest.mark.asyncio
async def test_synthesize_http_non_200_raises(monkeypatch):
    """A non-200 from the synthesis endpoint is an engine failure."""
    captured: list[dict] = []
    handler = _build_handler(
        captured,
        synth_status=503,
        synth_json={"error": "overloaded"},
    )
    transport = httpx.MockTransport(handler)
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    with pytest.raises(RuntimeError, match="503"):
        await engine.synthesize("hi")


@pytest.mark.asyncio
async def test_synthesize_missing_mp3_url_raises(monkeypatch):
    """success: true but no MP3 URL fields -> clear error."""
    captured: list[dict] = []
    handler = _build_handler(
        captured,
        synth_json={"success": True, "mp3StreamingUrl": None, "mp3DownloadUrl": None},
    )
    transport = httpx.MockTransport(handler)
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    with pytest.raises(RuntimeError, match="MP3 URL"):
        await engine.synthesize("hi")

    assert len(captured) == 1  # no fetch attempted


@pytest.mark.asyncio
async def test_synthesize_mp3_fetch_non_200_raises(monkeypatch):
    """A non-200 on the MP3 fetch is an engine failure."""
    captured: list[dict] = []
    handler = _build_handler(captured, mp3_status=404)
    transport = httpx.MockTransport(handler)
    _patch_decode(monkeypatch)
    engine = IrodoriEngine(url=_SYNTH_URL, transport=transport)

    with pytest.raises(RuntimeError, match="MP3 fetch failed"):
        await engine.synthesize("hi")
