"""MCP tool definitions for StackChan.

These definitions describe the ESP32 device's tool interface.
Used by the local stub router (mcp_router.py) for testing.
The stdio MCP server (stdio_server.py) defines its own tool list for MCP client.
"""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field

# ---------------------------------------------------------------------------
# Tool parameter schemas
# ---------------------------------------------------------------------------


class SetHeadAnglesParams(BaseModel):
    yaw: int = Field(description="Yaw angle in degrees")
    pitch: int = Field(description="Pitch angle in degrees")
    speed: int = Field(default=50, description="Movement speed (1-100)")


class SetLedColorParams(BaseModel):
    r: int = Field(ge=0, le=255, description="Red (0-255)")
    g: int = Field(ge=0, le=255, description="Green (0-255)")
    b: int = Field(ge=0, le=255, description="Blue (0-255)")


class SetVolumeParams(BaseModel):
    volume: int = Field(ge=0, le=100, description="Volume level (0-100)")


# ---------------------------------------------------------------------------
# Tool registry (ESP32 device tools — used by local stub router)
# ---------------------------------------------------------------------------

TOOL_DEFINITIONS: list[dict[str, Any]] = [
    {
        "name": "self.robot.get_head_angles",
        "description": "Get current head servo angles (yaw, pitch).",
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
    {
        "name": "self.robot.set_head_angles",
        "description": "Set head servo angles.",
        "inputSchema": SetHeadAnglesParams.model_json_schema(),
    },
    {
        "name": "self.robot.set_led_color",
        "description": "Set LED color (RGB).",
        "inputSchema": SetLedColorParams.model_json_schema(),
    },
    {
        "name": "self.audio_speaker.set_volume",
        "description": "Set speaker volume (0-100).",
        "inputSchema": SetVolumeParams.model_json_schema(),
    },
    {
        "name": "self.camera.take_photo",
        "description": "Take a photo with the device camera. Returns JPEG image.",
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
    {
        "name": "self.get_device_status",
        "description": "Get device status (battery, connection, angles).",
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
]
