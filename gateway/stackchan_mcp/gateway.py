"""Two-faced gateway: bridges MCP client (stdio MCP) and ESP32 (WebSocket MCP).

MCP client sees a standard MCP server via stdio.
ESP32 sees a WebSocket server that sends MCP client requests.
This module orchestrates both sides.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from typing import Any

from aiohttp import web

from .capture_server import (
    create_capture_app,
    stage_avatar_set,
    stage_firmware_image,
    unstage_firmware_image,
)
from .esp32_client import ESP32Manager
from .firmware_image import (
    DEFAULT_BOARD_NAME,
    FirmwareImageError,
    validate_firmware_image,
)
from .mdns_advertiser import MdnsAdvertiser
from .agent_status import AgentSnapshot, AgentStatusStore

logger = logging.getLogger(__name__)

# The device shows an alert for ~3 s and stops audio before it starts the
# HTTP GET (Application::UpgradeFirmware), so allow some slack.
FIRMWARE_FETCH_TIMEOUT_S = 30.0


def _tool_text_json(result: object) -> object:
    """Decode the first text block of a device tools/call result as JSON."""
    if not isinstance(result, dict):
        return None
    for item in result.get("content") or []:
        if isinstance(item, dict) and item.get("type") == "text":
            try:
                return json.loads(item.get("text", ""))
            except (TypeError, ValueError):
                return None
    return None

BEAT_MODE_LISTEN_STOP_TIMEOUT_S = 3.0


class Gateway:
    """Main gateway orchestrator.

    Holds the ESP32 manager and provides the bridge between
    the stdio MCP server (MCP client side) and the ESP32 device.

    Also runs an HTTP capture server for receiving photos from ESP32.
    """

    def __init__(self):
        self.esp32 = ESP32Manager()
        self.agent_status_store = AgentStatusStore()
        self._running = False
        self._http_runner: web.AppRunner | None = None
        # Phase 4.5 avatar: kept so load_avatar_set can stage payloads
        # against the same web.Application that serves /avatar_set/{id}.
        self._capture_app: web.Application | None = None
        self._mdns_advertiser: MdnsAdvertiser | None = None

    @property
    def vision_url(self) -> str:
        """URL for ESP32 to POST captured photos to.

        VISION_URL can be set to a complete public capture URL for remote
        access setups such as Tailscale Funnel. Otherwise VISION_HOST should
        be the LAN IP of the host running this gateway, as seen from the ESP32
        (e.g. something like 192.168.x.y on a typical home network). Falls
        back to "127.0.0.1" with a warning if unset; in that case the ESP32
        will not be able to reach the capture endpoint over the network.
        """
        explicit_url = os.getenv("VISION_URL")
        if explicit_url:
            return explicit_url

        host = os.getenv("VISION_HOST")
        if not host:
            logger.warning(
                "VISION_URL/VISION_HOST not set; defaulting to 127.0.0.1. "
                "ESP32 will not reach the capture endpoint unless "
                "VISION_HOST is set to this host's LAN IP or VISION_URL is "
                "set to a full capture URL."
            )
            host = "127.0.0.1"
        port = int(os.getenv("CAPTURE_PORT", "8766"))
        return f"http://{host}:{port}/capture"

    @property
    def vision_token(self) -> str:
        """Bearer token expected by the capture endpoint.

        VISION_TOKEN can be set separately. By default, reuse the ESP32
        WebSocket token so remote capture uploads are protected whenever the
        gateway itself is protected.
        """
        return (
            os.getenv("VISION_TOKEN")
            or os.getenv("STACKCHAN_TOKEN")
            or os.getenv("BEARER_TOKEN")
            or ""
        )

    @property
    def audio_hook_url(self) -> str:
        """URL receiving device-driven listen captures as Ogg/Opus.

        STACKCHAN_AUDIO_HOOK_URL enables the device-driven listen
        capture path (wake word / button / LCD touch): the gateway
        packs inbound Opus frames into an Ogg container and POSTs to
        this URL on ``listen.stop``. The capture path is **disabled**
        when this is unset — stackchan-mcp's primary listen model
        remains MCP-client-driven (the ``listen()`` tool), and
        device-driven capture only makes sense when an external
        service is set up to receive the audio.
        """
        return os.getenv("STACKCHAN_AUDIO_HOOK_URL", "")

    @property
    def audio_hook_token(self) -> str:
        """Bearer token expected by the audio hook endpoint.

        STACKCHAN_AUDIO_HOOK_TOKEN can be set separately. Falls back to
        STACKCHAN_TOKEN so a single-token setup works out of the box.
        """
        return (
            os.getenv("STACKCHAN_AUDIO_HOOK_TOKEN")
            or os.getenv("STACKCHAN_TOKEN")
            or os.getenv("BEARER_TOKEN")
            or ""
        )

    @property
    def pcm_token(self) -> str:
        """Bearer token expected by the /pcm HTTP endpoint.

        Separate token from the ESP32 WebSocket / capture upload because
        the /pcm endpoint authorises external PCM producers (e.g. the
        SAIVerse voice-tts addon) — a different trust boundary from the
        device-to-gateway authentication. Falls back to STACKCHAN_TOKEN
        / BEARER_TOKEN when STACKCHAN_PCM_TOKEN is not configured so
        single-token local development keeps working.
        """
        return (
            os.getenv("STACKCHAN_PCM_TOKEN")
            or os.getenv("STACKCHAN_TOKEN")
            or os.getenv("BEARER_TOKEN")
            or ""
        )

    async def start(self, *, advertise_mdns: bool = True) -> None:
        """Start the ESP32 WebSocket server and HTTP capture server."""
        host = os.getenv("HOST", "0.0.0.0")
        ws_port = int(os.getenv("WS_PORT", os.getenv("PORT", "8765")))
        capture_port = int(os.getenv("CAPTURE_PORT", "8766"))

        # Start WebSocket server for ESP32
        await self.esp32.start(
            host,
            ws_port,
            vision_url=self.vision_url,
            vision_token=self.vision_token,
            audio_hook_url=self.audio_hook_url,
            audio_hook_token=self.audio_hook_token,
        )

        # Start HTTP capture server. Hosts /capture, /pcm, and the
        # Phase 4.5 avatar /avatar_set/{short_id} endpoint on the same
        # web.Application. The PCM endpoint forwards into
        # send_pcm_stream, so we hand it the active Gateway instance so
        # it can reach esp32 + tts_lock.
        app = create_capture_app(
            capture_token=self.vision_token,
            pcm_token=self.pcm_token,
            gateway=self,
        )
        self._capture_app = app
        self._http_runner = web.AppRunner(app)
        await self._http_runner.setup()
        site = web.TCPSite(self._http_runner, host, capture_port)
        await site.start()

        if advertise_mdns:
            self._mdns_advertiser = MdnsAdvertiser()
            try:
                await self._mdns_advertiser.start(host=host, port=ws_port, path="/")
            except Exception as exc:  # pragma: no cover - exact zeroconf errors vary by host
                logger.warning("mDNS advertisement failed: %s", exc)
                self._mdns_advertiser = None
        else:
            self._mdns_advertiser = None

        self._running = True
        logger.info(
            "Gateway started: WS on %s:%d, capture on %s:%d, vision_url=%s",
            host, ws_port, host, capture_port, self.vision_url,
        )

    async def stop(self) -> None:
        """Stop the gateway."""
        # Cancel any active pose-stream follower before the rest of the
        # shutdown sequence closes gateway-side services.
        try:
            from .follow_pose_stream import stop_follow

            await stop_follow()
        except Exception as exc:  # pragma: no cover - defensive
            logger.warning("follow_pose_stream shutdown failed: %s", exc)

        # Likewise cancel any active LED-stream follower so its WiFi
        # power-save lease is released before services close.
        try:
            from .follow_led_stream import stop_follow as stop_led_follow

            await stop_led_follow()
        except Exception as exc:  # pragma: no cover - defensive
            logger.warning("follow_led_stream shutdown failed: %s", exc)

        try:
            from .beat import stop_beat_mode

            await stop_beat_mode(
                listen_stop_timeout_s=BEAT_MODE_LISTEN_STOP_TIMEOUT_S,
            )
        except Exception as exc:  # pragma: no cover - defensive
            logger.warning("beat mode shutdown failed: %s", exc)

        self._running = False
        if self._mdns_advertiser:
            try:
                await self._mdns_advertiser.stop()
            except Exception as exc:  # pragma: no cover - exact zeroconf errors vary by host
                logger.warning("mDNS advertisement shutdown failed: %s", exc)
            finally:
                self._mdns_advertiser = None
        if self._http_runner:
            await self._http_runner.cleanup()
            self._http_runner = None
        self._capture_app = None
        await self.esp32.stop()
        logger.info("Gateway stopped")

    async def update_agent_snapshot(self, snapshot: AgentSnapshot):
        """Store a privacy-limited snapshot from an Agent adapter."""
        return await self.agent_status_store.update(snapshot)

    async def get_agent_dashboard(self) -> dict[str, Any]:
        """Return the latest display-ready state in fixed Codex/Claude order."""
        views = await self.agent_status_store.get_all()
        task_priority = {"in_progress": 0, "pending": 1, "completed": 2}
        agents: list[dict[str, Any]] = []
        for agent, view in zip(("codex", "claude"), views, strict=True):
            snapshot = view.snapshot
            agents.append(
                {
                    "agent": agent,
                    "online": view.online,
                    "stale": view.stale,
                    "status": snapshot.status if snapshot else "idle",
                    "stage": snapshot.stage if snapshot else "",
                    "tasks": [
                        {"id": task.id, "text": task.text, "status": task.status}
                        for task in sorted(
                            snapshot.tasks if snapshot else (),
                            key=lambda item: task_priority[item.status],
                        )
                    ],
                }
            )
        return {"agents": agents}

    # ---- Phase 4.5 avatar (saiverse-stackchan-addon) ------------------

    @property
    def avatar_set_base_url(self) -> str:
        """Base URL the device should hit for /avatar_set/{short_id}.

        Reuses vision_url so the device reaches this gateway over the
        same network path it already uses for camera POSTs (VISION_HOST
        / VISION_URL). The trailing /capture component is stripped.
        """
        url = self.vision_url
        if url.endswith("/capture"):
            return url[: -len("/capture")]
        return url

    async def load_avatar_set(
        self,
        archive_path: str,
        mode: str,
        timeout: float = 60.0,
        device_id: str | None = None,
    ) -> dict:
        """Stage an avatar set + notify the device + await its reply.

        See docs/intent/stackchan_avatar_pipeline.md §C in the SAIVerse
        repository for the protocol. ``archive_path`` is the path to a
        local file containing the raw RGB565 payload (gateway expects
        the addon to have already converted PNG/PIL output to RGB565).
        ``device_id`` is forwarded to
        :meth:`ESP32Manager.send_avatar_set_fetch`; omitted (None)
        targets the default device exactly as before phase2.
        """
        if self._capture_app is None:
            return {"ok": False, "error": "gateway_not_started"}
        if not os.path.exists(archive_path):
            return {"ok": False, "error": f"archive_not_found: {archive_path}"}

        with open(archive_path, "rb") as f:
            payload = f.read()

        kimg_bytes = 160 * 120 * 2  # 38_400 — matches AvatarSet::kImageBytes
        expected = {
            "layered": 14 * kimg_bytes,   # 537_600
            "matrix":  90 * kimg_bytes,   # 3_456_000
        }.get(mode)
        if expected is None:
            return {"ok": False, "error": f"unknown_mode: {mode}"}
        if len(payload) != expected:
            return {
                "ok": False,
                "error": f"size_mismatch: got={len(payload)} expected={expected} (mode={mode})",
            }

        try:
            short_id, token, sha256 = await stage_avatar_set(
                self._capture_app, mode, payload
            )
        except ValueError as exc:
            return {"ok": False, "error": str(exc)}

        url = f"{self.avatar_set_base_url}/avatar_set/{short_id}"
        if device_id is None:
            result = await self.esp32.send_avatar_set_fetch(
                url=url,
                token=token,
                mode=mode,
                checksum=sha256,
                expected_size=len(payload),
                timeout=timeout,
            )
        else:
            result = await self.esp32.send_avatar_set_fetch(
                url=url,
                token=token,
                mode=mode,
                checksum=sha256,
                expected_size=len(payload),
                timeout=timeout,
                device_id=device_id,
            )
        # Surface the staging metadata for caller-side observability.
        result.setdefault("checksum", sha256)
        result["bytes_transferred"] = len(payload) if result.get("ok") else 0
        return result


    # ---- Over-the-air firmware upgrade ---------------------------------

    def _firmware_base_url(self, connection: object) -> str:
        """Base URL the device should GET /firmware/{id} from.

        Prefer the gateway address the device's own WebSocket reached:
        it is on the device's network by construction, whereas
        VISION_URL may point at a public relay (e.g. Tailscale Funnel)
        that the device's OTA client cannot download through.
        """
        host = getattr(connection, "local_host", None)
        if host and not host.startswith("127.") and host != "::1":
            if ":" in host:
                host = f"[{host}]"
            port = int(os.getenv("CAPTURE_PORT", "8766"))
            return f"http://{host}:{port}"
        return self.avatar_set_base_url

    async def _device_system_info(self, device_id: str) -> dict | None:
        result, error = await self.esp32.call_tool(
            "self.get_system_info", {}, device_id=device_id
        )
        if error:
            return None
        info = _tool_text_json(result)
        return info if isinstance(info, dict) else None

    async def upgrade_firmware(
        self,
        firmware_path: str,
        timeout: float = 180.0,
        device_id: str | None = None,
    ) -> dict:
        """Validate a local xiaozhi.bin, serve it once, and OTA the device.

        Waits for the device to download it, reboot and reconnect, then
        confirms the running app's ELF SHA-256 matches the image.
        """
        path = os.path.expanduser(firmware_path)
        if not os.path.isfile(path):
            return {"ok": False, "error": f"firmware_not_found: {firmware_path}"}
        with open(path, "rb") as f:
            payload = f.read()

        connection = self.esp32.connection_for(device_id)
        if connection is None or not connection.connected:
            # Still validate so a bad file is reported even when offline.
            try:
                validate_firmware_image(payload)
            except FirmwareImageError as exc:
                return {"ok": False, "error": str(exc), "sent_to_device": False}
            return {"ok": False, "error": "no_device_connected", "sent_to_device": False}
        target_id = connection.device_id

        before = await self._device_system_info(target_id)
        board_name = DEFAULT_BOARD_NAME
        if before and isinstance(before.get("board"), dict):
            board_name = before["board"].get("name") or DEFAULT_BOARD_NAME
        try:
            image = validate_firmware_image(payload, board_name=board_name)
        except FirmwareImageError as exc:
            return {"ok": False, "error": str(exc), "sent_to_device": False}
        if self._capture_app is None:
            return {"ok": False, "error": "gateway_not_started", "sent_to_device": False}

        before_app = (before or {}).get("application") or {}
        old_session = connection.session_id
        image_id, fetched = await stage_firmware_image(self._capture_app, payload)
        url = f"{self._firmware_base_url(connection)}/firmware/{image_id}"
        logger.info(
            "OTA upgrade: device=%s image=%s version=%s",
            target_id, image.sha256[:12], image.version,
        )
        base = {
            "device_id": target_id,
            "image": image.as_dict(),
            "before": {
                "version": before_app.get("version"),
                "compile_time": before_app.get("compile_time"),
                "elf_sha256": before_app.get("elf_sha256"),
                "partition": (before or {}).get("ota", {}).get("label"),
            },
        }

        # The device schedules the upgrade on its main loop before the
        # tools/call reply is queued behind it, so the reply usually never
        # arrives (the gateway sees a response timeout while the download
        # is already running). Only an explicit error counts as a refusal;
        # the download itself is the real acknowledgement.
        started = time.monotonic()
        call_task = asyncio.create_task(
            self.esp32.call_tool(
                "self.upgrade_firmware", {"url": url}, device_id=target_id
            )
        )
        fetch_task = asyncio.create_task(fetched.wait())
        try:
            await asyncio.wait(
                {call_task, fetch_task},
                timeout=FIRMWARE_FETCH_TIMEOUT_S,
                return_when=asyncio.FIRST_COMPLETED,
            )
            if call_task.done() and not fetched.is_set():
                result, error = call_task.result()
                timed_out = bool(error) and "Timeout" in str(error.get("message", ""))
                if (error and not timed_out) or (
                    isinstance(result, dict) and result.get("isError")
                ):
                    await unstage_firmware_image(self._capture_app, image_id)
                    msg = error.get("message", str(error)) if error else str(result)
                    return {
                        **base, "ok": False, "sent_to_device": False,
                        "error": f"device_rejected: {msg}",
                    }
            remaining = FIRMWARE_FETCH_TIMEOUT_S - (time.monotonic() - started)
            await asyncio.wait_for(fetched.wait(), max(remaining, 0.1))
        except asyncio.TimeoutError:
            await unstage_firmware_image(self._capture_app, image_id)
            return {
                **base, "ok": False, "sent_to_device": False,
                "error": f"device_did_not_download: no GET on {url.rsplit('/', 1)[0]}/... "
                f"within {FIRMWARE_FETCH_TIMEOUT_S:.0f}s (is the capture port reachable from the device?)",
            }
        finally:
            fetch_task.cancel()
            if not call_task.done():
                call_task.cancel()

        deadline = started + max(timeout, FIRMWARE_FETCH_TIMEOUT_S + 30.0)
        new_conn = None
        while time.monotonic() < deadline:
            c = self.esp32.connection_for(target_id)
            if (
                c is not None and c.connected and c.session_id != old_session
                and c.initialized and c.tools_discovered
            ):
                new_conn = c
                break
            await asyncio.sleep(0.5)
        if new_conn is None:
            still_same = (
                (c := self.esp32.connection_for(target_id)) is not None
                and c.connected and c.session_id == old_session
            )
            return {
                **base, "ok": False, "sent_to_device": True,
                "error": (
                    "upgrade_failed_on_device: device downloaded the image but did not "
                    "reboot (see its screen / serial log); it is still running the old firmware"
                    if still_same else
                    "no_reconnect: device downloaded the image but did not reconnect to this "
                    "gateway in time (it may have joined another gateway via mDNS)"
                ),
            }

        after = await self._device_system_info(target_id)
        after_app = (after or {}).get("application") or {}
        out = {
            **base,
            "sent_to_device": True,
            "after": {
                "version": after_app.get("version"),
                "compile_time": after_app.get("compile_time"),
                "elf_sha256": after_app.get("elf_sha256"),
                "partition": (after or {}).get("ota", {}).get("label"),
            },
            "elapsed_s": round(time.monotonic() - started, 1),
        }
        if after_app.get("elf_sha256") == image.elf_sha256:
            out["ok"] = True
        else:
            out["ok"] = False
            out["error"] = (
                "running_image_mismatch: device rebooted but is not running the new "
                "image (rolled back, or system info unavailable)"
            )
        return out


# Singleton gateway instance, shared between stdio server and ESP32 manager
_gateway: Gateway | None = None


def get_gateway() -> Gateway:
    """Get or create the singleton gateway."""
    global _gateway
    if _gateway is None:
        _gateway = Gateway()
    return _gateway
