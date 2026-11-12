"""Pre-flight checks for an ESP32 app image before an over-the-air upgrade.

The device firmware writes whatever ``self.upgrade_firmware`` downloads
into the inactive OTA slot, and ESP-IDF only rejects it at the very end
(``esp_ota_end``: chip id / checksum / SHA-256). A same-chip image built
for another board passes those checks and can leave the device without a
working network path, so the gateway refuses anything that does not look
like a StackChan ``xiaozhi.bin`` before the device is ever contacted.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass

# esp_image_header_t (24 bytes) + esp_image_segment_header_t (8 bytes)
# precede esp_app_desc_t in the first segment of an app image.
_IMAGE_MAGIC = 0xE9
_APP_DESC_OFFSET = 24 + 8
_APP_DESC_MAGIC = 0xABCD5432
_CHIP_ID_OFFSET = 12
_HASH_APPENDED_OFFSET = 23

ESP32S3_CHIP_ID = 9
PROJECT_NAME = "xiaozhi"
DEFAULT_BOARD_NAME = "stackchan"
# ota_0 / ota_1 size in firmware/partitions/v2/16m.csv.
MAX_APP_BYTES = 0x3F0000
# Anything smaller cannot hold the header + app descriptor + hash.
_MIN_APP_BYTES = _APP_DESC_OFFSET + 256 + 32


class FirmwareImageError(ValueError):
    """The file is not an image this device can safely boot."""


@dataclass(frozen=True)
class FirmwareImageInfo:
    size: int
    sha256: str
    version: str
    project_name: str
    compile_time: str
    idf_version: str
    elf_sha256: str

    def as_dict(self) -> dict[str, object]:
        return {
            "size": self.size,
            "sha256": self.sha256,
            "version": self.version,
            "project_name": self.project_name,
            "compile_time": self.compile_time,
            "idf_version": self.idf_version,
            "elf_sha256": self.elf_sha256,
        }


def _cstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def validate_firmware_image(
    data: bytes,
    *,
    board_name: str = DEFAULT_BOARD_NAME,
    max_size: int = MAX_APP_BYTES,
) -> FirmwareImageInfo:
    """Return image metadata, or raise FirmwareImageError on any mismatch."""
    if len(data) < _MIN_APP_BYTES:
        raise FirmwareImageError(f"too_small: {len(data)} bytes")
    if len(data) > max_size:
        raise FirmwareImageError(
            f"too_large: {len(data)} bytes > OTA slot {max_size} bytes"
        )
    if data[0] != _IMAGE_MAGIC:
        raise FirmwareImageError(
            f"not_an_esp_app_image: magic=0x{data[0]:02x} (merged/full-flash "
            "images are not accepted; use build/xiaozhi.bin)"
        )
    chip_id = struct.unpack_from("<H", data, _CHIP_ID_OFFSET)[0]
    if chip_id != ESP32S3_CHIP_ID:
        raise FirmwareImageError(
            f"wrong_chip: chip_id={chip_id} (expected {ESP32S3_CHIP_ID} = ESP32-S3)"
        )
    if data[_HASH_APPENDED_OFFSET] != 1:
        raise FirmwareImageError("no_appended_sha256: image cannot be verified")
    if hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise FirmwareImageError("sha256_mismatch: image is truncated or corrupted")

    desc = data[_APP_DESC_OFFSET:]
    if struct.unpack_from("<I", desc, 0)[0] != _APP_DESC_MAGIC:
        raise FirmwareImageError("no_app_descriptor")
    project = _cstr(desc[48:80])
    if project != PROJECT_NAME:
        raise FirmwareImageError(
            f"wrong_project: {project!r} (expected {PROJECT_NAME!r})"
        )
    if board_name and (board_name.encode() + b"\0") not in data:
        raise FirmwareImageError(
            f"wrong_board: image does not contain board name {board_name!r}"
        )

    return FirmwareImageInfo(
        size=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
        version=_cstr(desc[16:48]),
        project_name=project,
        compile_time=f"{_cstr(desc[96:112])} {_cstr(desc[80:96])}",
        idf_version=_cstr(desc[112:144]),
        elf_sha256=desc[144:176].hex(),
    )
