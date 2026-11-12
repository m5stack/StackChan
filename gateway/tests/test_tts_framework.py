"""Tests for the TTS framework skeleton (Issue #70 PR1).

Concrete engine implementations land in follow-up PRs; here we only
exercise the abstract base, the registry, and the orchestrator's
validation / error surface.
"""

from __future__ import annotations

import pytest

from stackchan_mcp.tts import (
    DEFAULT_VOICE,
    EngineRegistry,
    TTSEngine,
    get_registry,
    synthesize_and_send,
)
from stackchan_mcp.tts.orchestrator import (
    TTS_ENGINE_ENV_VAR,
    _resolve_default_engine,
)


class _FakeEngine(TTSEngine):
    """Minimal in-test engine used to exercise registry behaviour."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.calls: list[tuple[str, dict[str, object]]] = []

    async def synthesize(self, text: str, **opts: object) -> bytes:
        self.calls.append((text, dict(opts)))
        return b""


def test_tts_engine_is_abstract():
    """TTSEngine cannot be instantiated directly."""
    with pytest.raises(TypeError):
        TTSEngine()  # type: ignore[abstract]


def test_registry_rejects_engine_with_empty_name():
    """Registering an engine without a name is a programmer error."""
    reg = EngineRegistry()
    engine = _FakeEngine(name="")
    with pytest.raises(ValueError):
        reg.register(engine)


def test_registry_register_get_names_roundtrip():
    """register/get/names form a consistent set."""
    reg = EngineRegistry()
    engine = _FakeEngine(name="voicevox")

    reg.register(engine)

    assert reg.get("voicevox") is engine
    assert reg.get("nonexistent") is None
    assert reg.names() == ["voicevox"]


def test_registry_register_replaces_same_name():
    """Re-registering the same name swaps the engine — useful for tests."""
    reg = EngineRegistry()
    first = _FakeEngine(name="voicevox")
    second = _FakeEngine(name="voicevox")

    reg.register(first)
    reg.register(second)

    assert reg.get("voicevox") is second
    assert reg.names() == ["voicevox"]


def test_registry_names_are_sorted():
    """names() is sorted so callers (e.g. error messages) get a stable order."""
    reg = EngineRegistry()
    reg.register(_FakeEngine(name="zeta"))
    reg.register(_FakeEngine(name="alpha"))
    reg.register(_FakeEngine(name="mu"))

    assert reg.names() == ["alpha", "mu", "zeta"]


def test_get_registry_returns_singleton():
    """The default registry is process-wide (a singleton)."""
    assert get_registry() is get_registry()


def test_default_voice_constant():
    """The default voice is the planned VOICEVOX engine."""
    assert DEFAULT_VOICE == "voicevox"


def test_tts_engine_defaults_to_no_emoji_style_support():
    engine = _FakeEngine(name="voicevox")
    assert engine.supports_emoji_style is False


@pytest.mark.asyncio
async def test_synthesize_and_send_rejects_missing_text():
    """No text -> ValueError before any engine lookup."""
    reg = EngineRegistry()
    with pytest.raises(ValueError, match="text"):
        await synthesize_and_send({}, registry=reg)


@pytest.mark.asyncio
async def test_synthesize_and_send_rejects_empty_text():
    """Whitespace-only text is rejected the same as empty."""
    reg = EngineRegistry()
    with pytest.raises(ValueError, match="text"):
        await synthesize_and_send({"text": "   "}, registry=reg)


@pytest.mark.asyncio
async def test_synthesize_and_send_rejects_non_string_text():
    """Non-string text -> ValueError (defensive against bad MCP clients)."""
    reg = EngineRegistry()
    with pytest.raises(ValueError, match="text"):
        await synthesize_and_send({"text": 42}, registry=reg)


@pytest.mark.asyncio
async def test_synthesize_and_send_unregistered_voice_raises():
    """Unregistered voice -> NotImplementedError, listing what's available."""
    reg = EngineRegistry()
    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send({"text": "hello"}, registry=reg)

    msg = str(exc_info.value)
    assert "voicevox" in msg
    assert "(none)" in msg


@pytest.mark.asyncio
async def test_synthesize_and_send_requires_gateway():
    """Validation passes but pipeline refuses without a gateway argument.

    Surfacing a clear RuntimeError beats silently synthesising PCM that
    has nowhere to go. Validation tests can still exercise the
    argument-shape surface without spinning up a Gateway.
    """
    reg = EngineRegistry()
    reg.register(_FakeEngine(name="voicevox"))

    with pytest.raises(RuntimeError, match="gateway"):
        await synthesize_and_send({"text": "hello"}, registry=reg)


@pytest.mark.asyncio
async def test_synthesize_and_send_voice_default_falls_back():
    """Empty/missing 'voice' falls back to DEFAULT_VOICE."""
    reg = EngineRegistry()

    # Empty string voice -> default
    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send({"text": "hello", "voice": ""}, registry=reg)
    assert DEFAULT_VOICE in str(exc_info.value)

    # Non-string voice -> default (not a TypeError)
    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send({"text": "hello", "voice": 123}, registry=reg)
    assert DEFAULT_VOICE in str(exc_info.value)


@pytest.mark.asyncio
async def test_synthesize_and_send_lists_available_engines_in_error():
    """Error message names what *is* registered so callers can pick correctly."""
    reg = EngineRegistry()
    reg.register(_FakeEngine(name="alpha"))
    reg.register(_FakeEngine(name="beta"))

    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send(
            {"text": "hello", "voice": "voicevox"}, registry=reg
        )

    msg = str(exc_info.value)
    assert "alpha" in msg
    assert "beta" in msg


# ---------------------------------------------------------------------------
# STACKCHAN_TTS_ENGINE default-engine override (Issue #286)
# ---------------------------------------------------------------------------


def test_resolve_default_engine_unset_is_voicevox(monkeypatch):
    """No STACKCHAN_TTS_ENGINE -> the built-in DEFAULT_VOICE."""
    monkeypatch.delenv(TTS_ENGINE_ENV_VAR, raising=False)
    assert _resolve_default_engine() == DEFAULT_VOICE


def test_resolve_default_engine_env_override(monkeypatch):
    """STACKCHAN_TTS_ENGINE selects the default engine."""
    monkeypatch.setenv(TTS_ENGINE_ENV_VAR, "irodori")
    assert _resolve_default_engine() == "irodori"


def test_resolve_default_engine_blank_env_ignored(monkeypatch):
    """A blank/whitespace value is ignored, falling back to DEFAULT_VOICE."""
    monkeypatch.setenv(TTS_ENGINE_ENV_VAR, "   ")
    assert _resolve_default_engine() == DEFAULT_VOICE


@pytest.mark.asyncio
async def test_synthesize_and_send_uses_env_default_engine(monkeypatch):
    """With no 'voice' arg, the engine looked up comes from the env override.

    The unregistered-engine error names the resolved engine, which lets
    us confirm the orchestrator consulted STACKCHAN_TTS_ENGINE without
    standing up a full gateway.
    """
    monkeypatch.setenv(TTS_ENGINE_ENV_VAR, "irodori")
    reg = EngineRegistry()

    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send({"text": "hello"}, registry=reg)

    assert "irodori" in str(exc_info.value)


@pytest.mark.asyncio
async def test_explicit_voice_overrides_env_default(monkeypatch):
    """An explicit 'voice' argument still wins over STACKCHAN_TTS_ENGINE."""
    monkeypatch.setenv(TTS_ENGINE_ENV_VAR, "irodori")
    reg = EngineRegistry()

    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send(
            {"text": "hello", "voice": "voicevox"}, registry=reg
        )

    msg = str(exc_info.value)
    assert "voicevox" in msg
    assert "irodori" not in msg


@pytest.mark.asyncio
async def test_omitted_voice_with_env_unset_uses_default_voice(monkeypatch):
    """Omitted 'voice' + unset env -> DEFAULT_VOICE is the looked-up engine."""
    monkeypatch.delenv(TTS_ENGINE_ENV_VAR, raising=False)
    reg = EngineRegistry()

    with pytest.raises(NotImplementedError) as exc_info:
        await synthesize_and_send({"text": "hello"}, registry=reg)

    assert DEFAULT_VOICE in str(exc_info.value)
