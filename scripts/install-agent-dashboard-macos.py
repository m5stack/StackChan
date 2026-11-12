#!/usr/bin/env python3
"""为当前 macOS 用户安装网关和两个只读会话监视器。"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import plistlib
import secrets
import shutil
import subprocess
import sys
import time
from pathlib import Path


def private_write(path: Path, data: bytes) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-ip", required=True, help="电脑的局域网 IPv4 地址")
    args = parser.parse_args()
    ipaddress.IPv4Address(args.host_ip)
    if sys.platform != "darwin":
        parser.error("此安装器仅支持 macOS")
    root = Path(__file__).resolve().parents[1]
    gateway = root / "gateway"
    uv = shutil.which("uv")
    if uv is None:
        parser.error("请先安装 uv")
    # Background jobs use an installed wheel outside Desktop. This also makes
    # their runtime independent of checkout moves and editable-import paths.
    runtime = Path.home() / ".local/share/stackchan-agent-dashboard"
    runtime.mkdir(parents=True, exist_ok=True, mode=0o700)
    python = runtime / ".venv/bin/python"
    if not python.exists():
        subprocess.run([uv, "venv", str(runtime / ".venv"), "--python", "3.12"], check=True)
    requirements = runtime / "requirements.txt"
    subprocess.run([uv, "export", "--project", str(gateway), "--frozen", "--no-dev",
                    "--no-emit-project", "--no-hashes", "--format", "requirements-txt",
                    "--output-file", str(requirements)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([uv, "pip", "install", "--python", str(python),
                    "--constraint", str(requirements),
                    "--reinstall-package", "stackchan-mcp", str(gateway)], check=True)
    config_dir = Path.home() / ".config/stackchan-agent-dashboard"
    config_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    config_file = config_dir / "config.json"
    config = json.loads(config_file.read_text()) if config_file.exists() else {
        "token": secrets.token_hex(16), "http_port": 8767,
        "ws_port": 18765, "capture_port": 18766,
    }
    config["host_ip"] = args.host_ip
    private_write(config_file, json.dumps(config, indent=2).encode())
    connection = config_dir / "设备连接信息.txt"
    private_write(connection, (
        f"StackChan Agent 看板\n地址：http://{args.host_ip}:{config['http_port']}\n"
        f"令牌：{config['token']}\n\n"
        "在 SETUP → Apps → Agent 看板 中输入地址和令牌，测试成功后保存。\n"
        "此文件含本地服务令牌，请勿上传到仓库。\n"
    ).encode())
    logs = Path.home() / "Library/Logs/StackChanAgentDashboard"
    logs.mkdir(parents=True, exist_ok=True, mode=0o700)
    agents = Path.home() / "Library/LaunchAgents"
    agents.mkdir(parents=True, exist_ok=True)
    env = {
        "STACKCHAN_TOKEN": config["token"], "PYTHONUNBUFFERED": "1",
        "HOST": "127.0.0.1", "WS_PORT": str(config["ws_port"]),
        "CAPTURE_PORT": str(config["capture_port"]),
        "MCP_HTTP_HOST": "0.0.0.0", "MCP_HTTP_PORT": str(config["http_port"]),
        "MCP_HTTP_ALLOWED_HOSTS": args.host_ip,
        "STACKCHAN_MCP_URL": f"http://127.0.0.1:{config['http_port']}/mcp",
        "VISION_HOST": "127.0.0.1",
    }
    commands = {
        "gateway": ["-m", "stackchan_mcp", "serve", "--transport", "streamable-http", "--no-mdns"],
        "codex": ["-m", "stackchan_mcp.agent_adapters.runner", "codex-watch"],
        "claude": ["-m", "stackchan_mcp.agent_adapters.runner", "claude-watch"],
    }
    domain = f"gui/{os.getuid()}"
    for name, command in commands.items():
        label = f"io.stackchan.agent-dashboard.{name}"
        plist = agents / f"{label}.plist"
        private_write(plist, plistlib.dumps({
            "Label": label, "ProgramArguments": [str(python), *command],
            "WorkingDirectory": str(runtime), "EnvironmentVariables": env,
            "RunAtLoad": True, "KeepAlive": True, "ThrottleInterval": 10,
            "StandardOutPath": str(logs / f"{name}.log"),
            "StandardErrorPath": str(logs / f"{name}.error.log"),
        }))
        subprocess.run(["launchctl", "bootout", f"{domain}/{label}"],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # bootout returns before launchd has finished unloading the old job.
        for attempt in range(20):
            result = subprocess.run(["launchctl", "bootstrap", domain, str(plist)],
                                    check=False, capture_output=True, text=True)
            if result.returncode == 0:
                break
            if attempt == 19:
                raise RuntimeError(f"无法启动 {label}: {result.stderr.strip()}")
            time.sleep(0.25)
        print(f"已安装：{label}")
    print(f"设备连接信息：{connection}")
    print(f"日志目录：{logs}")
    print("已启用登录后自动启动；请在设备完成地址、令牌和 Wi-Fi 配置。")


if __name__ == "__main__":
    main()
