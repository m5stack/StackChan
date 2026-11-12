"""WebSocket server for ESP32 connections.

This module is retained for backward compatibility and testing.
In production, use the Gateway (gateway.py) which orchestrates
both the ESP32 WebSocket server and the stdio MCP server.
"""

from __future__ import annotations

import logging

from .esp32_client import ESP32Manager

logger = logging.getLogger(__name__)


async def run_server(host: str = "0.0.0.0", port: int = 8765) -> None:
    """Start the ESP32 WebSocket server standalone (for testing)."""
    manager = ESP32Manager()
    await manager.start(host, port)
    logger.info("ESP32 WebSocket server running on ws://%s:%d", host, port)

    # Keep running until interrupted
    try:
        import asyncio
        await asyncio.Future()  # Run forever
    finally:
        await manager.stop()
