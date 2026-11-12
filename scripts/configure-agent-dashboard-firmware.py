#!/usr/bin/env python3
"""从本机网关配置生成不会提交到 Git 的个人固件默认值。"""

import argparse
import ipaddress
import json
import os
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config", type=Path,
        default=Path.home() / ".config/stackchan-agent-dashboard/config.json",
        help="macOS 网关安装器生成的本机配置文件",
    )
    args = parser.parse_args()
    try:
        config = json.loads(args.config.read_text())
        host = str(ipaddress.IPv4Address(config["host_ip"]))
        port = config["http_port"]
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError("port")
        url = f"http://{host}:{port}"
        token = config["token"]
        if not isinstance(token, str) or not token or len(token.encode()) > 256:
            raise ValueError("token")
        if any(ord(char) < 32 or ord(char) > 126 for char in token):
            raise ValueError("token")
    except (OSError, ValueError, KeyError, TypeError):
        parser.error("无法读取有效配置；请先运行网关安装器或检查 --config 文件。")
    target = (Path(__file__).resolve().parents[1] / "firmware/main/apps/"
              "app_agent_tasks/agent_tasks_local_defaults.h")
    source = ("#pragma once\n"
              f"#define AGENT_TASKS_DEFAULT_URL {json.dumps(url)}\n"
              f"#define AGENT_TASKS_DEFAULT_TOKEN {json.dumps(token)}\n")
    temporary = None
    try:
        fd, temporary = tempfile.mkstemp(prefix=".agent-defaults-", dir=target.parent)
        with os.fdopen(fd, "w") as stream:
            os.fchmod(stream.fileno(), 0o600)
            stream.write(source)
        os.replace(temporary, target)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)
    print("已生成本机默认配置（Git 忽略，权限 0600）；未输出令牌。")


if __name__ == "__main__":
    main()
