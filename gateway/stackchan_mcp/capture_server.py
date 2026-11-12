"""HTTP capture server for receiving photos from ESP32 and PCM from external producers.

Two POST endpoints share this server:

- ``POST /capture``: ESP32's camera.Explain() uploads JPEG photos as
  multipart/form-data (fields: ``question`` text + ``file`` JPEG).
  Authenticated via ``CAPTURE_TOKEN_KEY`` (the gateway's ``vision_token``).
  The server saves the JPEG to ``~/.stackchan/captures/`` and returns the
  file path so the MCP client can view the image via the Read tool.

- ``POST /pcm``: External producers (the SAIVerse voice-tts addon, etc.)
  upload PCM audio for the device's speaker as a streaming body
  (Content-Type: application/octet-stream, Transfer-Encoding: chunked).
  Authenticated separately via ``PCM_TOKEN_KEY``. The request body is
  fed directly into :func:`stackchan_mcp.tts.send_pcm_stream` so the
  audio reaches the device with low latency, without buffering the
  whole utterance.

The PCM endpoint is the entry point of the gateway's "external PCM
input" path — the receiving counterpart of the stdio ``say()`` MCP tool.
``say()`` synthesises audio with a registered TTS engine inside the
gateway; ``POST /pcm`` lets external producers (which already did the
synthesis themselves, e.g. with a voice-cloning model the gateway does
not host) push the finished PCM through the same back-half pipeline
(:func:`send_pcm_stream`).

Required PCM request headers:

- ``Authorization: Bearer <PCM_TOKEN>`` — token comparison against
  ``PCM_TOKEN_KEY`` (gateway's ``pcm_token`` property)
- ``X-Sample-Rate: <int>`` — sample rate of the source PCM (e.g. 32000).
  The gateway resamples to the device's 16 kHz before Opus encoding.
- ``X-Channels: 1`` (optional, defaults to 1) — only mono is supported
  for now (the device decoder is configured for mono).
- ``X-Message-Id: <str>`` (optional) — opaque identifier echoed back in
  the log line so the producer can correlate uploads with downstream
  device state.

The handler stores the active :class:`Gateway` instance in the
application's ``GATEWAY_KEY`` so it can dispatch to ``send_pcm_stream``
without coupling :mod:`capture_server` to the gateway module at import
time (lazy import inside the handler keeps the optional ``[tts]``
extra unnecessary for capture-only deployments).
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import os
import secrets
import sys
import time
from dataclasses import dataclass
from typing import TYPE_CHECKING, AsyncIterator

from aiohttp import web

if TYPE_CHECKING:
    from .gateway import Gateway

logger = logging.getLogger(__name__)

CAPTURE_DIR = os.path.expanduser("~/.stackchan/captures")
CAPTURE_TOKEN_KEY = web.AppKey("capture_token", str)
PCM_TOKEN_KEY = web.AppKey("pcm_token", str)
GATEWAY_KEY: web.AppKey = web.AppKey("gateway", object)

# Phase 4.5 avatar (saiverse-stackchan-addon): in-memory staging for
# one-time avatar set downloads. See docs/intent/stackchan_avatar_pipeline.md
# §C-2 in the SAIVerse repository.
AVATAR_SETS_KEY = web.AppKey("avatar_sets", dict)
AVATAR_SETS_LOCK_KEY = web.AppKey("avatar_sets_lock", asyncio.Lock)

# A staging entry is GC'd if it hasn't been fetched within this window.
AVATAR_SET_STAGING_TTL_SEC = 120.0


# One-time firmware images for the gateway ``upgrade_firmware`` tool. The
# device's OTA client (firmware/main/ota.cc ``Ota::Upgrade``) sends no
# Authorization header, so the 128-bit random id in the URL is the
# credential: it is single-use and expires after the TTL below.
FIRMWARE_IMAGES_KEY = web.AppKey("firmware_images", dict)
FIRMWARE_IMAGES_LOCK_KEY = web.AppKey("firmware_images_lock", asyncio.Lock)
FIRMWARE_STAGING_TTL_SEC = 120.0


@dataclass
class _FirmwareStaging:
    payload: bytes
    created_at: float
    fetched: asyncio.Event


@dataclass(frozen=True)
class _AvatarStaging:
    token: str
    mode: str
    payload: bytes
    sha256: str
    created_at: float


# Per-route upload cap for the JPEG capture endpoint. The PCM endpoint
# intentionally streams arbitrarily long payloads (multi-minute TTS),
# so the application-wide ``client_max_size`` is disabled and each
# route enforces its own limit. JPEG captures from the ESP32 camera
# top out around 200 KB at full resolution; 8 MiB is generous headroom
# against a misbehaving / malicious uploader without inviting unbounded
# disk consumption on the gateway host.
CAPTURE_MAX_BYTES = 8 * 1024 * 1024


def _is_authorized(auth_header: str, expected_token: str) -> bool:
    """Return whether the bearer auth header matches the expected token."""
    return auth_header == f"Bearer {expected_token}"


async def handle_capture(request: web.Request) -> web.Response:
    """Handle photo upload from ESP32."""
    expected_token = request.app[CAPTURE_TOKEN_KEY]
    if expected_token and not _is_authorized(
        request.headers.get("Authorization", ""), expected_token
    ):
        logger.warning("Capture upload auth rejected")
        return web.Response(
            text='{"error": "Unauthorized"}',
            status=401,
            content_type="application/json",
        )

    # Per-route body cap. The application-wide client_max_size is
    # disabled because /pcm streams arbitrary-length audio, so
    # /capture's defense lives here. Reject up front based on the
    # advertised Content-Length when available, and enforce again
    # while streaming so a misadvertised header cannot bypass the cap.
    content_length = request.content_length
    if content_length is not None and content_length > CAPTURE_MAX_BYTES:
        logger.warning(
            "Capture upload rejected: Content-Length %d exceeds %d",
            content_length, CAPTURE_MAX_BYTES,
        )
        return web.Response(
            text=json.dumps(
                {"error": f"Upload exceeds {CAPTURE_MAX_BYTES} bytes"}
            ),
            status=413,
            content_type="application/json",
        )

    os.makedirs(CAPTURE_DIR, exist_ok=True)

    reader = await request.multipart()
    question = ""
    image_path = ""
    bytes_written = 0

    async for part in reader:
        if part.name == "question":
            # Decode defensively: a malformed (non-UTF-8) question field must
            # not turn a photo upload into a 500. The question is advisory
            # metadata, so replacing undecodable bytes is preferable to
            # dropping the whole capture.
            question = (await part.read()).decode("utf-8", errors="replace")
        elif part.name == "file":
            timestamp = int(time.time() * 1000)
            filename = f"capture_{timestamp}.jpg"
            image_path = os.path.join(CAPTURE_DIR, filename)
            with open(image_path, "wb") as f:
                while True:
                    chunk = await part.read_chunk(8192)
                    if not chunk:
                        break
                    bytes_written += len(chunk)
                    if bytes_written > CAPTURE_MAX_BYTES:
                        # Overran the cap mid-stream — delete the
                        # partial file and bail out with 413 so the
                        # gateway host disk does not fill up.
                        f.close()
                        try:
                            os.remove(image_path)
                        except OSError:
                            pass
                        logger.warning(
                            "Capture upload truncated at %d bytes (cap %d)",
                            bytes_written, CAPTURE_MAX_BYTES,
                        )
                        return web.Response(
                            text=json.dumps(
                                {"error": f"Upload exceeds {CAPTURE_MAX_BYTES} bytes"}
                            ),
                            status=413,
                            content_type="application/json",
                        )
                    f.write(chunk)

    if image_path and os.path.exists(image_path):
        file_size = os.path.getsize(image_path)
        logger.info(
            "Captured photo: %s (%d bytes), question: %s",
            image_path,
            file_size,
            question,
        )
        result = json.dumps({
            "image_path": image_path,
            "size_bytes": file_size,
            "question": question,
        })
        return web.Response(text=result, content_type="application/json")

    return web.Response(
        text='{"error": "No image received"}',
        status=400,
        content_type="application/json",
    )


async def stage_avatar_set(
    app: web.Application,
    mode: str,
    payload: bytes,
) -> tuple[str, str, str]:
    """Stage an avatar set for one-time HTTP download.

    Returns (short_id, token, sha256). The caller hands these to the
    device via WS avatar_set_fetch; the device performs a GET against
    /avatar_set/{short_id} with Authorization: Bearer <token>.

    The staging entry is consumed on the first successful fetch and
    GC'd after AVATAR_SET_STAGING_TTL_SEC if never fetched.
    """
    if mode not in ("layered", "matrix"):
        raise ValueError(f"unknown avatar mode: {mode}")

    short_id = secrets.token_hex(8)
    token = secrets.token_urlsafe(32)
    sha256 = "sha256:" + hashlib.sha256(payload).hexdigest()

    staging = _AvatarStaging(
        token=token,
        mode=mode,
        payload=payload,
        sha256=sha256,
        created_at=time.time(),
    )

    sets = app[AVATAR_SETS_KEY]
    async with app[AVATAR_SETS_LOCK_KEY]:
        # Best-effort GC of stale entries before inserting.
        now = time.time()
        expired = [
            k for k, v in sets.items()
            if now - v.created_at > AVATAR_SET_STAGING_TTL_SEC
        ]
        for k in expired:
            sets.pop(k, None)
        sets[short_id] = staging

    logger.info(
        "Staged avatar set: short_id=%s mode=%s bytes=%d sha256=%s",
        short_id, mode, len(payload), sha256,
    )
    return short_id, token, sha256


async def handle_avatar_set_fetch(request: web.Request) -> web.Response:
    """Serve a staged avatar set (one-time)."""
    short_id = request.match_info.get("short_id", "")
    if not short_id:
        return web.Response(status=400, text="missing short_id")

    sets = request.app[AVATAR_SETS_KEY]
    # Validate the request fully (existence, TTL, auth) before consuming
    # the staged entry. An unauthenticated probe must not be able to
    # invalidate a legitimate transfer just by guessing the short_id,
    # and a real fetch that fails auth due to a transient header issue
    # must still find the entry on retry.
    async with request.app[AVATAR_SETS_LOCK_KEY]:
        staging = sets.get(short_id)
        if staging is None:
            return web.Response(status=404, text="not_found_or_consumed")

        if time.time() - staging.created_at > AVATAR_SET_STAGING_TTL_SEC:
            # Expired — drop the slot so it doesn't linger.
            sets.pop(short_id, None)
            return web.Response(status=410, text="staging_expired")

        auth = request.headers.get("Authorization", "")
        if auth != f"Bearer {staging.token}":
            logger.warning(
                "Avatar set fetch auth rejected for short_id=%s", short_id
            )
            return web.Response(status=401, text="unauthorized")

        # Auth confirmed: consume the one-time entry now.
        sets.pop(short_id, None)

    logger.info(
        "Serving avatar set: short_id=%s mode=%s bytes=%d",
        short_id, staging.mode, len(staging.payload),
    )
    return web.Response(
        body=staging.payload,
        content_type="application/octet-stream",
        headers={
            "X-Avatar-Mode": staging.mode,
            "X-Avatar-Sha256": staging.sha256,
            "Content-Length": str(len(staging.payload)),
        },
    )


async def stage_firmware_image(
    app: web.Application, payload: bytes
) -> tuple[str, asyncio.Event]:
    """Stage a firmware image for one unauthenticated, one-time download.

    Returns (image_id, fetched). ``fetched`` is set when the device has
    started downloading the image (the entry is consumed at that point).
    """
    image_id = secrets.token_urlsafe(16)
    staging = _FirmwareStaging(
        payload=payload, created_at=time.time(), fetched=asyncio.Event()
    )
    images = app[FIRMWARE_IMAGES_KEY]
    async with app[FIRMWARE_IMAGES_LOCK_KEY]:
        now = time.time()
        for k in [
            k for k, v in images.items()
            if now - v.created_at > FIRMWARE_STAGING_TTL_SEC
        ]:
            images.pop(k, None)
        images[image_id] = staging
    logger.info("Staged firmware image: bytes=%d", len(payload))
    return image_id, staging.fetched


async def unstage_firmware_image(app: web.Application, image_id: str) -> None:
    async with app[FIRMWARE_IMAGES_LOCK_KEY]:
        app[FIRMWARE_IMAGES_KEY].pop(image_id, None)


async def handle_firmware_fetch(request: web.Request) -> web.Response:
    """Serve a staged firmware image exactly once."""
    image_id = request.match_info.get("image_id", "")
    async with request.app[FIRMWARE_IMAGES_LOCK_KEY]:
        staging = request.app[FIRMWARE_IMAGES_KEY].pop(image_id, None)
    if staging is None:
        return web.Response(status=404, text="not_found_or_consumed")
    if time.time() - staging.created_at > FIRMWARE_STAGING_TTL_SEC:
        return web.Response(status=410, text="staging_expired")
    staging.fetched.set()
    logger.info(
        "Serving firmware image to %s: bytes=%d",
        request.remote, len(staging.payload),
    )
    return web.Response(
        body=staging.payload,
        content_type="application/octet-stream",
        headers={"Content-Length": str(len(staging.payload))},
    )


async def _pcm_chunks_from_request(
    request: web.Request,
) -> AsyncIterator[bytes]:
    """Yield PCM byte chunks from the request body.

    ``request.content`` is an :class:`aiohttp.StreamReader` that delivers
    raw bytes as the chunked transfer arrives. ``iter_chunked(size)``
    breaks the stream into ``<= size`` byte pieces, matching the
    ``send_pcm_stream`` contract (any chunk size, internally re-aligned
    to Opus frame boundaries).

    Empty chunks (= heartbeat / cancellation tick) reach
    ``send_pcm_stream`` unchanged and are handled as no-ops there.
    """
    async for chunk in request.content.iter_chunked(8192):
        yield chunk


async def handle_pcm(request: web.Request) -> web.Response:
    """Stream PCM bytes from an external producer to the connected device.

    See the module docstring for the request shape (headers, token,
    body framing). The handler authenticates, validates the sample
    rate header, then hands the body off to
    :func:`stackchan_mcp.tts.send_pcm_stream`.

    Returns 200 with a JSON summary on success (frame count, duration,
    source label), 401 on token mismatch, 400 on missing /
    malformed sample-rate header, 503 when no device is connected, or
    500 with a clean error string on encoding / push failures (mirrors
    the error-class discipline of the stdio ``say()`` tool).
    """
    expected_token = request.app[PCM_TOKEN_KEY]
    if expected_token and not _is_authorized(
        request.headers.get("Authorization", ""), expected_token
    ):
        logger.warning("PCM upload auth rejected")
        return web.Response(
            text='{"error": "Unauthorized"}',
            status=401,
            content_type="application/json",
        )

    rate_header = request.headers.get("X-Sample-Rate", "")
    try:
        source_rate = int(rate_header)
    except (TypeError, ValueError):
        return web.Response(
            text=json.dumps(
                {"error": f"Missing or invalid X-Sample-Rate header: {rate_header!r}"}
            ),
            status=400,
            content_type="application/json",
        )
    if source_rate <= 0:
        # Non-positive rates would crash resample_pcm16_linear with a
        # ZeroDivisionError (which the RuntimeError handler below does
        # not translate) and never produce a valid frame anyway. Reject
        # at the boundary so the caller gets a clean 400 instead of
        # an internal server error trail.
        return web.Response(
            text=json.dumps(
                {"error": f"X-Sample-Rate must be a positive integer: {rate_header!r}"}
            ),
            status=400,
            content_type="application/json",
        )

    channels_header = request.headers.get("X-Channels", "1")
    try:
        channels = int(channels_header)
    except (TypeError, ValueError):
        channels = 1
    if channels != 1:
        # send_pcm_stream is configured for mono via DEVICE_CHANNELS. Multi-
        # channel sources would need downmix before they get here; rejecting
        # them up front is clearer than silently mixing.
        return web.Response(
            text=json.dumps(
                {"error": f"Only mono PCM is supported, got channels={channels}"}
            ),
            status=400,
            content_type="application/json",
        )

    message_id = request.headers.get("X-Message-Id", "")
    source_label = f"http_pcm:{message_id}" if message_id else "http_pcm"

    gateway = request.app[GATEWAY_KEY]
    if gateway is None:
        return web.Response(
            text='{"error": "Gateway not available"}',
            status=503,
            content_type="application/json",
        )

    # Lazy import: tts.send_pcm_stream pulls in opuslib, which is in the
    # ``[tts]`` extra. Capture-only deployments must keep working
    # without the extra, so we only require it when /pcm is actually
    # used.
    try:
        from .tts import send_pcm_stream
    except ImportError as exc:
        return web.Response(
            text=json.dumps(
                {
                    "error": f"PCM endpoint requires the [tts] extra: {exc}",
                }
            ),
            status=500,
            content_type="application/json",
        )

    try:
        result = await send_pcm_stream(
            gateway,
            _pcm_chunks_from_request(request),
            source_rate=source_rate,
            source_label=source_label,
        )
    except RuntimeError as exc:
        # send_pcm_stream raises RuntimeError on no-device / protocol
        # mismatch / opuslib missing / disconnect mid-stream. Translate
        # to a clean HTTP error rather than letting the traceback leak.
        message = str(exc)
        status = 503 if "no esp32" in message.lower() else 500
        return web.Response(
            text=json.dumps({"error": message}),
            status=status,
            content_type="application/json",
        )

    return web.Response(text=json.dumps(result), content_type="application/json")


def create_capture_app(
    capture_token: str = "",
    pcm_token: str = "",
    gateway: "Gateway | None" = None,
) -> web.Application:
    """Create the HTTP server application hosting /capture and /pcm.

    ``capture_token`` authenticates ESP32 photo uploads (legacy single-
    arg form is kept so existing tests keep working). ``pcm_token``
    authenticates external PCM producers; if omitted the gateway will
    accept any /pcm request, which matches the "no STACKCHAN_TOKEN set"
    fallback behaviour the rest of the gateway already uses for ad-hoc
    local development.

    ``gateway`` is the active :class:`Gateway` instance the /pcm handler
    dispatches to. May be ``None`` for tests of /capture alone; /pcm
    will return 503 in that case.
    """
    # Raise aiohttp's per-request body cap (default 1 MiB) effectively to
    # unlimited. The /pcm endpoint legitimately streams arbitrarily long PCM
    # utterances (multi-minute TTS, live audio mixes); a 1 MiB cap would
    # silently cut a chunked-transfer producer off mid-stream once its
    # cumulative body exceeded that limit — observed in practice with a
    # 200-second TTS push, which aborted around 36 s in (~2 MiB of
    # source-rate PCM through the transfer-encoding pipe).
    #
    # Use ``sys.maxsize`` rather than ``0``. Although ``0`` disables the cap
    # for ``request.read()`` / ``request.post()`` (both guard with
    # ``if 0 < max_size``), aiohttp's multipart reader does NOT special-case
    # ``0``: ``BodyPartReader.read()`` compares ``len(data) > client_max_size``
    # directly (aiohttp >= 3.14), so a client_max_size of 0 propagated into
    # ``request.multipart()`` rejects ANY non-empty multipart field with
    # HTTP 413. /capture reads its ``question`` form field via ``part.read()``,
    # so with ``client_max_size=0`` every photo upload carrying a non-empty
    # question 413s ("Failed to upload photo"). A large finite value avoids
    # that trap while keeping /pcm effectively unbounded. /capture's real
    # size limit is the explicit CAPTURE_MAX_BYTES check in handle_capture.
    app = web.Application(client_max_size=sys.maxsize)
    app[CAPTURE_TOKEN_KEY] = capture_token
    app[AVATAR_SETS_KEY] = {}
    app[AVATAR_SETS_LOCK_KEY] = asyncio.Lock()
    app[FIRMWARE_IMAGES_KEY] = {}
    app[FIRMWARE_IMAGES_LOCK_KEY] = asyncio.Lock()
    app[PCM_TOKEN_KEY] = pcm_token
    app[GATEWAY_KEY] = gateway
    app.router.add_post("/capture", handle_capture)
    app.router.add_get("/avatar_set/{short_id}", handle_avatar_set_fetch)
    app.router.add_get("/firmware/{image_id}", handle_firmware_fetch)
    app.router.add_post("/pcm", handle_pcm)
    return app
