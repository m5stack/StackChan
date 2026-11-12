"""Notification configuration for Stack-chan physical events."""

from __future__ import annotations

from dataclasses import dataclass
import logging
import os
from pathlib import Path
from typing import Any, Final

import yaml

from .event_log import DEFAULT_LOG_PATH, PATH_ENV_VAR

logger = logging.getLogger(__name__)

CONFIG_ENV_VAR: Final[str] = "STACKCHAN_NOTIFY_CONFIG"
CONFIG_FILENAME: Final[str] = "stackchan-mcp/notify.yml"


@dataclass(frozen=True)
class MessageTemplate:
    action: str
    template: str


@dataclass(frozen=True)
class NotifyConfig:
    legacy_event_enabled: bool
    channels_enabled: bool
    jsonl_enabled: bool
    jsonl_path: Path
    messages: dict[tuple[str, str], MessageTemplate]


DEFAULT_MESSAGE_TEMPLATES: Final[dict[tuple[str, str], MessageTemplate]] = {
    ("touch", "tap"): MessageTemplate(
        action="head_pat",
        template="head was tapped",
    ),
    ("touch", "stroke"): MessageTemplate(
        action="head_stroke",
        template="head was stroked for {duration_ms}ms",
    ),
}


def resolve_notify_config_path() -> Path | None:
    """Return the first existing notify.yml path, or None."""
    override = os.environ.get(CONFIG_ENV_VAR)
    if override:
        path = Path(override).expanduser()
        if path.exists():
            return path
        logger.warning(
            "%s points to a non-existent file: %s",
            CONFIG_ENV_VAR,
            path,
        )
        return None

    xdg_config_home = os.environ.get("XDG_CONFIG_HOME")
    candidates = []
    if xdg_config_home:
        candidates.append(Path(xdg_config_home).expanduser() / CONFIG_FILENAME)
    candidates.append(Path.home() / ".config" / CONFIG_FILENAME)

    for path in candidates:
        if path.exists():
            return path
    return None


def load_notify_config() -> NotifyConfig:
    """Load the notification config, falling back to all-OFF on errors."""
    path = resolve_notify_config_path()
    if path is None:
        return _default_config()

    try:
        with path.open("r", encoding="utf-8") as f:
            raw = yaml.safe_load(f)
        return _parse_config(raw)
    except (OSError, yaml.YAMLError, ValueError, TypeError) as exc:
        logger.warning(
            "Failed to load Stack-chan notify config from %s: %s",
            path,
            exc,
        )
        return _default_config()


def render_template(template: str, payload: dict[str, Any]) -> str:
    """Render a user template while preserving unknown placeholders.

    A malformed-but-yaml-valid template (e.g. ``{duration_ms.foo}`` or
    ``{unknown[0]}``) can raise ``AttributeError`` or ``TypeError`` from
    ``str.format_map``. These are caught here so a single bad user template
    cannot crash the channels dispatch path on every physical event; the
    original template string is returned as a defensive fallback.
    """
    try:
        return template.format_map(_SafeFormatDict(payload))
    except (IndexError, KeyError, ValueError, AttributeError, TypeError):
        return template


def _default_config() -> NotifyConfig:
    return NotifyConfig(
        legacy_event_enabled=False,
        channels_enabled=False,
        jsonl_enabled=False,
        jsonl_path=_resolve_jsonl_path(None),
        messages=_default_messages(),
    )


def _default_messages() -> dict[tuple[str, str], MessageTemplate]:
    return dict(DEFAULT_MESSAGE_TEMPLATES)


def _parse_config(raw: Any) -> NotifyConfig:
    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise ValueError("notify config root must be a mapping")

    legacy_event_enabled = _parse_enabled(raw, "legacy_event")
    channels_enabled = _parse_enabled(raw, "channels")
    jsonl_section = _parse_section(raw, "jsonl")
    jsonl_enabled = _parse_enabled(raw, "jsonl")
    jsonl_path = _resolve_jsonl_path(_optional_string(jsonl_section, "path"))
    messages = _parse_messages(raw.get("messages"))

    return NotifyConfig(
        legacy_event_enabled=legacy_event_enabled,
        channels_enabled=channels_enabled,
        jsonl_enabled=jsonl_enabled,
        jsonl_path=jsonl_path,
        messages=messages,
    )


def _parse_enabled(root: dict[Any, Any], section_name: str) -> bool:
    section = _parse_section(root, section_name)
    enabled = section.get("enabled", False)
    if not isinstance(enabled, bool):
        raise ValueError(f"{section_name}.enabled must be a boolean")
    return enabled


def _parse_section(root: dict[Any, Any], section_name: str) -> dict[Any, Any]:
    section = root.get(section_name, {})
    if section is None:
        return {}
    if not isinstance(section, dict):
        raise ValueError(f"{section_name} must be a mapping")
    return section


def _optional_string(section: dict[Any, Any], key: str) -> str | None:
    value = section.get(key)
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise ValueError(f"{key} must be a non-empty string")
    return value


def _parse_messages(raw_messages: Any) -> dict[tuple[str, str], MessageTemplate]:
    messages = _default_messages()
    if raw_messages is None:
        return messages
    if not isinstance(raw_messages, dict):
        raise ValueError("messages must be a mapping")

    for event_type, subtypes in raw_messages.items():
        if not isinstance(event_type, str) or not event_type:
            raise ValueError("messages event_type keys must be non-empty strings")
        if not isinstance(subtypes, dict):
            raise ValueError(f"messages.{event_type} must be a mapping")
        for subtype, message in subtypes.items():
            if not isinstance(subtype, str) or not subtype:
                raise ValueError("messages subtype keys must be non-empty strings")
            if not isinstance(message, dict):
                raise ValueError(f"messages.{event_type}.{subtype} must be a mapping")
            action = message.get("action")
            template = message.get("template")
            if not isinstance(action, str) or not action:
                raise ValueError(
                    f"messages.{event_type}.{subtype}.action must be a non-empty string"
                )
            if not isinstance(template, str) or not template:
                raise ValueError(
                    f"messages.{event_type}.{subtype}.template must be a non-empty string"
                )
            messages[(event_type, subtype)] = MessageTemplate(
                action=action,
                template=template,
            )
    return messages


def _resolve_jsonl_path(configured_path: str | None) -> Path:
    override = os.environ.get(PATH_ENV_VAR)
    if override:
        return _absolute_path(override)
    if configured_path:
        return _absolute_path(configured_path)
    return DEFAULT_LOG_PATH


def _absolute_path(raw_path: str) -> Path:
    path = Path(raw_path).expanduser()
    if path.is_absolute():
        return path
    return path.resolve()


class _SafeFormatDict(dict[str, Any]):
    def __missing__(self, key: str) -> "_MissingPlaceholder":
        return _MissingPlaceholder(key)


class _MissingPlaceholder:
    def __init__(self, key: str) -> None:
        self._key = key

    def __format__(self, spec: str) -> str:
        if spec:
            return "{" + self._key + ":" + spec + "}"
        return "{" + self._key + "}"

    def __str__(self) -> str:
        return "{" + self._key + "}"
