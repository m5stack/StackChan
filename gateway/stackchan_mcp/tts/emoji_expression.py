"""Emoji-driven avatar expression helpers for ``say`` text."""

from __future__ import annotations

import re


EMOJI_FACE_GROUPS: dict[str, tuple[str, ...]] = {
    "happy": ("😊", "😄", "😀", "😁", "🙂", "😆", "🥰", "😍", "😋", "🤗"),
    "sad": ("😢", "😭", "😞", "😔", "☹️", "🙁", "😿"),
    "surprised": ("😲", "😮", "😯", "😱", "🤯"),
    "embarrassed": ("😳", "😅", "🫣"),
    "thinking": ("🤔", "🧐", "💭"),
}

_EMOJI_TO_FACE: dict[str, str] = {
    emoji: face
    for face, emojis in EMOJI_FACE_GROUPS.items()
    for emoji in emojis
}
_MAPPED_EMOJIS = tuple(
    sorted(_EMOJI_TO_FACE, key=lambda emoji: len(emoji), reverse=True)
)

_EMOJI_BASE = (
    "["
    "\u00a9\u00ae"
    "\u2600-\u27bf"
    "\U0001f000-\U0001faff"
    "]"
)
_EMOJI_MODIFIER = "[\ufe0f\U0001f3fb-\U0001f3ff]*"
_EMOJI_SEQUENCE_RE = re.compile(
    f"(?:[0-9#*]\ufe0f?\u20e3|"
    f"{_EMOJI_BASE}{_EMOJI_MODIFIER}"
    f"(?:\u200d{_EMOJI_BASE}{_EMOJI_MODIFIER})*)"
)
_EMOJI_RESIDUE_RE = re.compile("[\ufe0f\u200d\U0001f3fb-\U0001f3ff\u20e3]")
_WHITESPACE_RE = re.compile(r"\s+")


def detect_emoji_face(text: str) -> str | None:
    """Return the face mapped from the first supported emoji in ``text``."""
    best_index: int | None = None
    best_emoji: str | None = None

    for emoji in _MAPPED_EMOJIS:
        index = text.find(emoji)
        if index < 0:
            continue
        if (
            best_index is None
            or index < best_index
            or (
                index == best_index
                and best_emoji is not None
                and len(emoji) > len(best_emoji)
            )
        ):
            best_index = index
            best_emoji = emoji

    if best_emoji is None:
        return None
    return _EMOJI_TO_FACE[best_emoji]


def contains_emoji(text: str) -> bool:
    """Return whether ``text`` contains any Unicode emoji-like sequence."""
    return _EMOJI_SEQUENCE_RE.search(text) is not None


def strip_emoji_for_plain_tts(text: str) -> str:
    """Remove emoji for engines that do not interpret them as style cues."""
    if not contains_emoji(text):
        return text

    stripped = _EMOJI_SEQUENCE_RE.sub(" ", text)
    stripped = _EMOJI_RESIDUE_RE.sub(" ", stripped)
    return _WHITESPACE_RE.sub(" ", stripped).strip()
