"""Over-the-air ``upgrade_firmware``: image pre-flight, one-time staging, flow."""

from __future__ import annotations

import asyncio
import hashlib
import json
import struct
import time

import aiohttp
import pytest
from aiohttp.test_utils import TestClient, TestServer

from stackchan_mcp import capture_server
from stackchan_mcp.capture_server import create_capture_app, stage_firmware_image
from stackchan_mcp.firmware_image import (
    FirmwareImageError,
    validate_firmware_image,
)
from stackchan_mcp.gateway import Gateway

ELF = bytes(range(32))

async_test = pytest.mark.asyncio


def make_image(
    *,
    chip_id: int = 9,
    project: bytes = b"xiaozhi",
    board: bytes = b"stackchan",
    magic: int = 0xE9,
    hash_appended: int = 1,
    body_len: int = 4096,
) -> bytes:
    header = bytearray(24)
    header[0] = magic
    header[1] = 1
    struct.pack_into("<H", header, 12, chip_id)
    header[23] = hash_appended
    desc = bytearray(256)
    struct.pack_into("<I", desc, 0, 0xABCD5432)
    desc[16:16 + 5] = b"2.2.6"
    desc[48:48 + len(project)] = project
    desc[80:88] = b"00:56:27"
    desc[96:107] = b"Oct 23 2026"
    desc[112:118] = b"v5.5.2"
    desc[144:176] = ELF
    body = bytearray(body_len)
    body[100:100 + len(board) + 1] = board + b"\0"
    data = bytes(header) + bytes(8) + bytes(desc) + bytes(body)
    return data + hashlib.sha256(data).digest()


# ---------------------------------------------------------------------------
# Pre-flight validation
# ---------------------------------------------------------------------------


def test_accepts_stackchan_image():
    info = validate_firmware_image(make_image())
    assert info.version == "2.2.6"
    assert info.project_name == "xiaozhi"
    assert info.elf_sha256 == ELF.hex()
    assert info.compile_time == "Oct 23 2026 00:56:27"


@pytest.mark.parametrize(
    ("data", "code"),
    [
        (b"\xe9" + bytes(100), "too_small"),
        (make_image(magic=0x00), "not_an_esp_app_image"),
        (make_image(chip_id=0), "wrong_chip"),  # classic ESP32 build
        (make_image(hash_appended=0), "no_appended_sha256"),
        (make_image(project=b"other_app"), "wrong_project"),
        (make_image(board=b"m5stack-core-s3"), "wrong_board"),
        (make_image()[:-1] + b"\x00", "sha256_mismatch"),
        (make_image()[:2000] + make_image()[-32:], "sha256_mismatch"),  # truncated
    ],
)
def test_rejects_bad_images(data, code):
    with pytest.raises(FirmwareImageError, match=code):
        validate_firmware_image(data)


def test_rejects_image_larger_than_ota_slot():
    with pytest.raises(FirmwareImageError, match="too_large"):
        validate_firmware_image(make_image(), max_size=1000)


# ---------------------------------------------------------------------------
# One-time staging endpoint
# ---------------------------------------------------------------------------


@async_test
async def test_firmware_url_is_single_use():
    app = create_capture_app()
    payload = make_image()
    image_id, fetched = await stage_firmware_image(app, payload)
    assert len(image_id) >= 22  # 128-bit random id
    async with TestClient(TestServer(app)) as client:
        resp = await client.get(f"/firmware/{image_id}")
        assert resp.status == 200
        assert resp.headers["Content-Length"] == str(len(payload))
        assert await resp.read() == payload
        assert fetched.is_set()
        again = await client.get(f"/firmware/{image_id}")
        assert again.status == 404
        assert (await client.get("/firmware/guess")).status == 404


@async_test
async def test_firmware_url_expires(monkeypatch):
    app = create_capture_app()
    image_id, fetched = await stage_firmware_image(app, make_image())
    real = time.time
    monkeypatch.setattr(
        capture_server.time, "time",
        lambda: real() + capture_server.FIRMWARE_STAGING_TTL_SEC + 1,
    )
    async with TestClient(TestServer(app)) as client:
        assert (await client.get(f"/firmware/{image_id}")).status == 410
    assert not fetched.is_set()


# ---------------------------------------------------------------------------
# Gateway.upgrade_firmware flow (fake device)
# ---------------------------------------------------------------------------


def _text(obj) -> dict:
    return {"content": [{"type": "text", "text": json.dumps(obj)}], "isError": False}


def _sysinfo(elf: str) -> dict:
    return {
        "application": {"version": "2.2.6", "compile_time": "x", "elf_sha256": elf},
        "ota": {"label": "ota_0"},
        "board": {"name": "stackchan"},
    }


class _Conn:
    def __init__(self, session_id: str):
        self.device_id = "dev"
        self.session_id = session_id
        self.connected = True
        self.initialized = True
        self.tools_discovered = True
        self.local_host = None


class _FakeESP32:
    def __init__(
        self,
        base_url: str,
        *,
        download: bool = True,
        reboot_elf: str | None = None,
        reply: str = "ok",
    ):
        self.reply = reply
        self.conn = _Conn("old")
        self.calls: list[str] = []
        self.base_url = base_url
        self.download = download
        self.reboot_elf = reboot_elf
        self.running_elf = "00" * 32

    def connection_for(self, device_id=None):
        return self.conn

    async def call_tool(self, name, arguments, *, device_id=None):
        self.calls.append(name)
        if name == "self.get_system_info":
            return _text(_sysinfo(self.running_elf)), None
        if name == "self.upgrade_firmware":
            if self.reply == "error":
                return None, {"code": -32000, "message": "Unknown tool: self.upgrade_firmware"}
            asyncio.get_running_loop().create_task(self._device(arguments["url"]))
            if self.reply == "timeout":
                # Real firmware: the reply is queued behind the upgrade itself.
                await asyncio.sleep(0.3)
                return None, {"code": -32000, "message": "Timeout waiting for ESP32 response (method=tools/call)"}
            return {"content": [{"type": "text", "text": "true"}], "isError": False}, None
        raise AssertionError(name)

    async def _device(self, url):
        if not self.download:
            return
        path = url.split("/firmware/", 1)[1]
        async with aiohttp.ClientSession() as s:
            async with s.get(f"{self.base_url}/firmware/{path}") as r:
                assert r.status == 200
                await r.read()
        await asyncio.sleep(0.2)
        self.running_elf = self.reboot_elf or ELF.hex()
        self.conn = _Conn("new")


async def _run(tmp_path, data, **fake_kwargs):
    gw = Gateway()
    app = create_capture_app()
    gw._capture_app = app
    path = tmp_path / "xiaozhi.bin"
    path.write_bytes(data)
    async with TestServer(app) as server:
        base = str(server.make_url("")).rstrip("/")
        fake = _FakeESP32(base, **fake_kwargs)
        gw.esp32 = fake
        gw._firmware_base_url = lambda _conn: base
        result = await gw.upgrade_firmware(str(path), timeout=60.0)
    return result, fake


@async_test
async def test_upgrade_happy_path(tmp_path):
    result, fake = await _run(tmp_path, make_image())
    assert result["ok"] is True, result
    assert result["after"]["elf_sha256"] == ELF.hex()
    assert fake.calls == [
        "self.get_system_info", "self.upgrade_firmware", "self.get_system_info",
    ]


@async_test
async def test_upgrade_succeeds_when_device_reply_times_out(tmp_path):
    result, _ = await _run(tmp_path, make_image(), reply="timeout")
    assert result["ok"] is True, result


@async_test
async def test_explicit_device_error_is_a_refusal(tmp_path):
    result, _ = await _run(tmp_path, make_image(), reply="error")
    assert result["ok"] is False
    assert result["sent_to_device"] is False
    assert "device_rejected" in result["error"]


@async_test
async def test_bad_image_is_never_sent_to_device(tmp_path):
    result, fake = await _run(tmp_path, make_image(chip_id=0))
    assert result["ok"] is False
    assert result["sent_to_device"] is False
    assert "wrong_chip" in result["error"]
    assert "self.upgrade_firmware" not in fake.calls


@async_test
async def test_rollback_is_reported(tmp_path):
    result, _ = await _run(tmp_path, make_image(), reboot_elf="11" * 32)
    assert result["ok"] is False
    assert "running_image_mismatch" in result["error"]


@async_test
async def test_no_download_unstages(tmp_path, monkeypatch):
    from stackchan_mcp import gateway as gateway_module

    monkeypatch.setattr(gateway_module, "FIRMWARE_FETCH_TIMEOUT_S", 0.5)
    result, _ = await _run(tmp_path, make_image(), download=False)
    assert result["ok"] is False
    assert "device_did_not_download" in result["error"]
