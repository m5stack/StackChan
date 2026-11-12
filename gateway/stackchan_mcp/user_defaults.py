"""User-local MCP tool default overlays."""

from __future__ import annotations

from collections.abc import Mapping
import json
import logging
from pathlib import Path
from typing import Any

import platformdirs

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - exercised on Python 3.10.
    import tomli as tomllib  # type: ignore[no-redef]

logger = logging.getLogger(__name__)

_CONFIG_APP_NAME = "stackchan-mcp"
_CONFIG_FILENAME = "user-defaults.toml"
_FOLLOW_POSE_TOOL = "stackchan_follow_pose_stream"
_FOLLOW_LED_TOOL = "stackchan_follow_led_stream"
_SCHEMA_DEFAULTS: dict[str, dict[str, Any]] = {
    _FOLLOW_POSE_TOOL: {
        "smoothing_window": 5,
        "downsample_hz": 20.0,
        "max_step_deg": 12.0,
        "speed_dps": 240,
        "flip_yaw": 1,
        "flip_pitch": 1,
        "pitch_center_deg": 45,
    },
    _FOLLOW_LED_TOOL: {
        "target": "",
        "led_count": 12,
        "max_fps": 30.0,
        "color_order": "grb",
        "source_filter": "",
        "frame_filter": "",
        "reconnect_initial_backoff_s": 1.5,
        "reconnect_max_backoff_s": 30.0,
    }
}
_USER_DEFAULTS_CACHE: dict[str, dict[str, Any]] | None = None


def _config_path() -> Path:
    return (
        platformdirs.user_config_path(
            _CONFIG_APP_NAME, appauthor=False, roaming=True
        )
        / _CONFIG_FILENAME
    ).expanduser().resolve()


def _format_value(value: Any) -> str:
    if isinstance(value, str):
        return json.dumps(value)
    return str(value)


def _is_int_value(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number_value(value: Any) -> bool:
    return isinstance(value, int | float) and not isinstance(value, bool)


def _is_type_compatible(value: Any, schema_default: Any) -> bool:
    if isinstance(schema_default, bool):
        return isinstance(value, bool)
    if isinstance(schema_default, int):
        return _is_int_value(value)
    if isinstance(schema_default, float):
        return _is_number_value(value)
    return isinstance(value, type(schema_default))


def _is_supported_value(tool_name: str, arg_name: str, value: Any) -> bool:
    schema_default = _SCHEMA_DEFAULTS[tool_name][arg_name]
    if not _is_type_compatible(value, schema_default):
        return False
    if tool_name == _FOLLOW_LED_TOOL:
        if arg_name == "target":
            return value in {"base_ring", "port_b", "port_c"}
        if arg_name == "led_count":
            return 1 <= value <= 256
        if arg_name == "max_fps":
            return 0 < float(value) <= 30
        if arg_name == "color_order":
            return value in {"grb", "rgb"}
        if arg_name in {"source_filter", "frame_filter"}:
            return value != ""
        if arg_name in {"reconnect_initial_backoff_s", "reconnect_max_backoff_s"}:
            return float(value) > 0
        return True
    if arg_name in {"flip_yaw", "flip_pitch"}:
        return value in (-1, 1)
    if arg_name == "pitch_center_deg":
        return 5 <= value <= 85
    if arg_name == "smoothing_window":
        return 1 <= value <= 20
    if arg_name == "downsample_hz":
        return 0 < float(value) <= 20
    if arg_name == "max_step_deg":
        return 0 < float(value) <= 30
    if arg_name == "speed_dps":
        return 1 <= value <= 240
    return True


def _warn_invalid_value(tool_name: str, arg_name: str, value: Any) -> None:
    schema_default = _SCHEMA_DEFAULTS[tool_name][arg_name]
    logger.warning(
        "[user-defaults] warning: invalid value for tool.%s.%s: %s — "
        "falling back to schema default (%s)",
        tool_name,
        arg_name,
        _format_value(value),
        _format_value(schema_default),
    )


def _validate_defaults(raw: Mapping[str, Any], path: Path) -> dict[str, dict[str, Any]]:
    tool_section = raw.get("tool", {})
    if tool_section == {}:
        return {}
    if not isinstance(tool_section, Mapping):
        logger.warning(
            "[user-defaults] warning: invalid tool table in %s — "
            "using schema defaults",
            path,
        )
        return {}

    defaults: dict[str, dict[str, Any]] = {}
    for tool_name, raw_tool_defaults in tool_section.items():
        if tool_name not in _SCHEMA_DEFAULTS:
            logger.warning(
                "[user-defaults] warning: unsupported tool defaults section "
                "tool.%s in %s",
                tool_name,
                path,
            )
            continue
        if not isinstance(raw_tool_defaults, Mapping):
            logger.warning(
                "[user-defaults] warning: invalid defaults table for tool.%s "
                "in %s",
                tool_name,
                path,
            )
            continue

        tool_defaults: dict[str, Any] = {}
        schema_defaults = _SCHEMA_DEFAULTS[tool_name]
        for arg_name, value in raw_tool_defaults.items():
            if arg_name not in schema_defaults:
                logger.warning(
                    "[user-defaults] warning: unsupported default "
                    "tool.%s.%s in %s",
                    tool_name,
                    arg_name,
                    path,
                )
                continue
            if not _is_supported_value(tool_name, arg_name, value):
                _warn_invalid_value(tool_name, arg_name, value)
                continue
            tool_defaults[arg_name] = value

        if tool_defaults:
            defaults[tool_name] = tool_defaults

    return defaults


def _read_user_defaults(path: Path) -> dict[str, dict[str, Any]]:
    try:
        with path.open("rb") as fp:
            raw = tomllib.load(fp)
    except FileNotFoundError:
        return {}
    except tomllib.TOMLDecodeError as exc:
        logger.warning(
            "[user-defaults] warning: failed to parse %s: %s — "
            "using schema defaults",
            path,
            exc,
        )
        return {}
    except OSError as exc:
        logger.warning(
            "[user-defaults] warning: failed to read %s: %s — "
            "using schema defaults",
            path,
            exc,
        )
        return {}

    if not raw:
        return {}
    return _validate_defaults(raw, path)


def load_user_defaults() -> dict[str, dict[str, Any]]:
    """Load and cache user-local MCP tool default overlays."""
    global _USER_DEFAULTS_CACHE

    if _USER_DEFAULTS_CACHE is None:
        try:
            path = _config_path()
        except Exception as exc:  # pragma: no cover - platformdirs/path failure.
            logger.warning(
                "[user-defaults] warning: failed to resolve config path: %s — "
                "using schema defaults",
                exc,
            )
            _USER_DEFAULTS_CACHE = {}
        else:
            _USER_DEFAULTS_CACHE = _read_user_defaults(path)
    return _USER_DEFAULTS_CACHE


def resolve_default(tool_name: str, arg_name: str, schema_default: Any) -> Any:
    """Return the configured default for one tool argument, if valid."""
    tool_defaults = load_user_defaults().get(tool_name)
    if not tool_defaults or arg_name not in tool_defaults:
        return schema_default

    value = tool_defaults[arg_name]
    if schema_default is None:
        return value
    if not _is_type_compatible(value, schema_default):
        return schema_default
    return value


def log_user_defaults_startup() -> None:
    """Emit startup diagnostics for the optional user-defaults file."""
    global _USER_DEFAULTS_CACHE

    try:
        path = _config_path()
    except Exception as exc:  # pragma: no cover - platformdirs/path failure.
        logger.warning(
            "[user-defaults] warning: failed to resolve config path: %s — "
            "using schema defaults",
            exc,
        )
        if _USER_DEFAULTS_CACHE is None:
            _USER_DEFAULTS_CACHE = {}
        return
    defaults = load_user_defaults()
    if not path.exists():
        logger.info(
            "[user-defaults] no config file found at %s — using schema defaults",
            path,
        )
        return
    if not defaults:
        logger.info(
            "[user-defaults] no valid overrides in %s — using schema defaults",
            path,
        )
        return

    logger.info("[user-defaults] loaded from %s", path)
    for tool_name, tool_defaults in defaults.items():
        formatted = ", ".join(
            f"{arg_name}={_format_value(value)}"
            for arg_name, value in tool_defaults.items()
        )
        logger.info("  tool.%s: %s", tool_name, formatted)


def _clear_user_defaults_cache_for_tests() -> None:
    global _USER_DEFAULTS_CACHE
    _USER_DEFAULTS_CACHE = None
