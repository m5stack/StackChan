"""Tests for user-local MCP tool default overlays."""

import logging
from pathlib import Path

import pytest

import stackchan_mcp.user_defaults as user_defaults


@pytest.fixture
def user_defaults_path(monkeypatch, tmp_path):
    config_dir = tmp_path / "config"
    config_path = config_dir / "user-defaults.toml"

    def fake_user_config_path(appname: str, **kwargs) -> Path:
        assert appname == "stackchan-mcp"
        return config_dir

    monkeypatch.setattr(
        user_defaults.platformdirs,
        "user_config_path",
        fake_user_config_path,
    )
    user_defaults._clear_user_defaults_cache_for_tests()
    yield config_path
    user_defaults._clear_user_defaults_cache_for_tests()


def test_resolve_default_returns_schema_default_when_config_absent(user_defaults_path):
    assert not user_defaults_path.exists()

    assert (
        user_defaults.resolve_default(
            "stackchan_follow_pose_stream",
            "smoothing_window",
            5,
        )
        == 5
    )


def test_resolve_default_returns_schema_default_for_empty_file(user_defaults_path):
    user_defaults_path.parent.mkdir(parents=True)
    user_defaults_path.write_text("", encoding="utf-8")

    assert (
        user_defaults.resolve_default(
            "stackchan_follow_pose_stream",
            "smoothing_window",
            5,
        )
        == 5
    )


def test_resolve_default_uses_valid_tool_overlay(user_defaults_path):
    user_defaults_path.parent.mkdir(parents=True)
    user_defaults_path.write_text(
        "[tool.stackchan_follow_pose_stream]\n"
        "smoothing_window = 1\n",
        encoding="utf-8",
    )

    assert (
        user_defaults.resolve_default(
            "stackchan_follow_pose_stream",
            "smoothing_window",
            5,
        )
        == 1
    )


def test_resolve_default_accepts_follow_led_color_order(user_defaults_path):
    user_defaults_path.parent.mkdir(parents=True)
    user_defaults_path.write_text(
        "[tool.stackchan_follow_led_stream]\n"
        'color_order = "rgb"\n',
        encoding="utf-8",
    )

    assert (
        user_defaults.resolve_default(
            "stackchan_follow_led_stream",
            "color_order",
            "grb",
        )
        == "rgb"
    )


def test_resolve_default_warns_and_falls_back_for_invalid_value(
    user_defaults_path,
    caplog,
):
    user_defaults_path.parent.mkdir(parents=True)
    user_defaults_path.write_text(
        "[tool.stackchan_follow_pose_stream]\n"
        'smoothing_window = "abc"\n',
        encoding="utf-8",
    )
    caplog.set_level(logging.WARNING, logger="stackchan_mcp.user_defaults")

    assert (
        user_defaults.resolve_default(
            "stackchan_follow_pose_stream",
            "smoothing_window",
            5,
        )
        == 5
    )
    assert (
        "invalid value for tool.stackchan_follow_pose_stream.smoothing_window"
        in caplog.text
    )
    assert "falling back to schema default (5)" in caplog.text


def test_resolve_default_rejects_invalid_follow_led_color_order(
    user_defaults_path,
    caplog,
):
    user_defaults_path.parent.mkdir(parents=True)
    user_defaults_path.write_text(
        "[tool.stackchan_follow_led_stream]\n"
        'color_order = "bgr"\n',
        encoding="utf-8",
    )
    caplog.set_level(logging.WARNING, logger="stackchan_mcp.user_defaults")

    assert (
        user_defaults.resolve_default(
            "stackchan_follow_led_stream",
            "color_order",
            "grb",
        )
        == "grb"
    )
    assert (
        "invalid value for tool.stackchan_follow_led_stream.color_order"
        in caplog.text
    )
