"""Tests for emoji-driven expression helpers."""

from __future__ import annotations

from stackchan_mcp.tts.emoji_expression import (
    detect_emoji_face,
    strip_emoji_for_plain_tts,
)


def test_detect_emoji_face_uses_first_supported_emoji():
    assert detect_emoji_face("先に sad 😢 あと happy 😊") == "sad"
    assert detect_emoji_face("先に thinking 🤔 あと happy 😊") == "thinking"


def test_detect_emoji_face_ignores_unmapped_emoji_before_mapped_one():
    assert detect_emoji_face("rocket 🚀 then happy 😊") == "happy"


def test_detect_emoji_face_returns_none_for_unmapped_or_plain_text():
    assert detect_emoji_face("plain text") is None
    assert detect_emoji_face("rocket 🚀") is None


def test_detect_emoji_face_supports_variation_selector_sequence():
    assert detect_emoji_face("かなしい ☹️") == "sad"


def test_strip_emoji_for_plain_tts_removes_all_emoji_and_collapses_spaces():
    text = "やったね 😊  rocket 🚀  thinking 🤔"
    assert strip_emoji_for_plain_tts(text) == "やったね rocket thinking"


def test_strip_emoji_for_plain_tts_preserves_word_boundaries():
    assert strip_emoji_for_plain_tts("hello😊world") == "hello world"


def test_strip_emoji_for_plain_tts_keeps_no_emoji_text_exactly():
    text = "hello   world"
    assert strip_emoji_for_plain_tts(text) == text


def test_strip_emoji_for_plain_tts_removes_joined_sequences():
    assert strip_emoji_for_plain_tts("work 🧑‍💻 done") == "work done"
