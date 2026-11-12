"""Tests for inlining take_photo captures as MCP ImageContent blocks.

``_photo_contents`` turns a take_photo text receipt (whose JSON carries
``image_path``) into text + an inline ``ImageContent`` JPEG so LLM
clients actually see the frame. Every failure mode must degrade to the
original text-only receipt — a broken image must never break the tool
call itself. Only files inside the capture directory are eligible, since
``image_path`` originates in a device response.
"""

from __future__ import annotations

import base64
import json

import pytest

from mcp.types import ImageContent, TextContent

from stackchan_mcp import capture_server
from stackchan_mcp.stdio_server import (
    _PHOTO_INLINE_MAX_BYTES,
    _dispatch_mcp_tool,
    _photo_contents,
)

#: Minimal JPEG-ish payload; content is never decoded, only base64'd.
_FAKE_JPEG = b"\xff\xd8\xff\xe0fake-jpeg-body\xff\xd9"


@pytest.fixture()
def capture_dir(tmp_path, monkeypatch):
    """Point the capture directory at tmp_path so fixtures count as inside it."""
    root = tmp_path / "captures"
    root.mkdir()
    monkeypatch.setattr(capture_server, "CAPTURE_DIR", str(root))
    return root


def _receipt(image_path) -> str:
    return json.dumps({"success": True, "image_path": str(image_path)})


def test_inlines_image_alongside_text(capture_dir):
    photo = capture_dir / "capture.jpg"
    photo.write_bytes(_FAKE_JPEG)

    out = _photo_contents(_receipt(photo))

    assert len(out) == 2
    text, image = out
    assert isinstance(text, TextContent)
    assert json.loads(text.text)["image_path"] == str(photo)
    assert isinstance(image, ImageContent)
    assert image.mimeType == "image/jpeg"
    assert base64.b64decode(image.data) == _FAKE_JPEG


def test_receipt_without_image_path_stays_text_only(capture_dir):
    out = _photo_contents(json.dumps({"success": False, "error": "no frame"}))

    assert len(out) == 1
    assert isinstance(out[0], TextContent)


def test_missing_file_degrades_to_text_only(capture_dir):
    out = _photo_contents(_receipt(capture_dir / "gone.jpg"))

    assert len(out) == 1
    assert isinstance(out[0], TextContent)


def test_path_outside_capture_dir_is_refused(capture_dir, tmp_path):
    outside = tmp_path / "secret.txt"
    outside.write_bytes(b"not yours to inline")

    out = _photo_contents(_receipt(outside))

    assert len(out) == 1
    assert isinstance(out[0], TextContent)


def test_symlink_escaping_capture_dir_is_refused(capture_dir, tmp_path):
    target = tmp_path / "outside.jpg"
    target.write_bytes(_FAKE_JPEG)
    link = capture_dir / "sneaky.jpg"
    link.symlink_to(target)

    out = _photo_contents(_receipt(link))

    assert len(out) == 1


def test_oversize_file_is_not_inlined(capture_dir):
    photo = capture_dir / "huge.jpg"
    photo.write_bytes(b"\xff\xd8" + b"x" * _PHOTO_INLINE_MAX_BYTES)

    out = _photo_contents(_receipt(photo))

    assert len(out) == 1


def test_non_jpeg_content_is_not_inlined(capture_dir):
    photo = capture_dir / "odd.jpg"
    photo.write_bytes(b"GIF89a not actually a jpeg")

    out = _photo_contents(_receipt(photo))

    assert len(out) == 1


def test_non_string_image_path_degrades_to_text_only(capture_dir):
    out = _photo_contents(json.dumps({"success": True, "image_path": 1}))

    assert len(out) == 1
    assert isinstance(out[0], TextContent)


def test_empty_file_is_not_inlined(capture_dir):
    photo = capture_dir / "empty.jpg"
    photo.write_bytes(b"")

    out = _photo_contents(_receipt(photo))

    assert len(out) == 1


def test_non_json_receipt_degrades_to_text_only(capture_dir):
    out = _photo_contents("took a photo (not json)")

    assert len(out) == 1
    assert out[0].text == "took a photo (not json)"


class _FakeEsp32:
    """Answers take_photo's nested ESP32 call with a canned receipt."""

    device_connected = True

    def __init__(self, receipt: str):
        self._receipt = receipt
        self.calls: list[tuple[str, dict]] = []

    async def call_tool(self, name, arguments, *, device_id=None):
        self.calls.append((name, arguments))
        return {"content": [{"type": "text", "text": self._receipt}]}, None

    def get_status(self):  # pragma: no cover - not used here
        return {}


class _FakeGateway:
    def __init__(self, receipt: str):
        self.esp32 = _FakeEsp32(receipt)


@pytest.mark.asyncio
async def test_dispatch_take_photo_returns_image_block(capture_dir):
    photo = capture_dir / "capture.jpg"
    photo.write_bytes(_FAKE_JPEG)
    gateway = _FakeGateway(_receipt(photo))

    out = await _dispatch_mcp_tool("take_photo", {}, gateway)

    assert gateway.esp32.calls[0][0] == "self.camera.take_photo"
    assert [type(c) for c in out] == [TextContent, ImageContent]
    assert base64.b64decode(out[1].data) == _FAKE_JPEG


@pytest.mark.asyncio
async def test_dispatch_take_photo_with_numeric_path_still_replies(capture_dir):
    gateway = _FakeGateway(json.dumps({"success": True, "image_path": 1}))

    out = await _dispatch_mcp_tool("take_photo", {}, gateway)

    assert [type(c) for c in out] == [TextContent]


@pytest.mark.asyncio
async def test_dispatch_other_tools_stay_text_only(capture_dir):
    photo = capture_dir / "capture.jpg"
    photo.write_bytes(_FAKE_JPEG)
    gateway = _FakeGateway(_receipt(photo))

    out = await _dispatch_mcp_tool("i2c_scan", {}, gateway)

    assert [type(c) for c in out] == [TextContent]
