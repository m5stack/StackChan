# 来源与整合说明

此项目以 [M5Stack/StackChan](https://github.com/m5stack/StackChan) 的 `28b9f0c8bd6acd81cb3c70f1a93c3e373b39d400` 为基础，增加官方 Mooncake Agent Tasks 应用。

`gateway/` 来自 [kisaragi-mochi/stackchan-mcp](https://github.com/kisaragi-mochi/stackchan-mcp)，附带本项目的 Agent 快照 HTTP 接口、Codex/Claude 适配器及回归测试。网关保持独立 Python 进程，不与固件静态链接。上游许可证完整保留在 `gateway/LICENSE` 与 `gateway/LICENSE-THIRD-PARTY`。

本次整合不包含上游替代固件的 SCServo_lib 文件，也不以替代固件覆盖官方固件。早期替代固件实验与原网关工作区保存在本机整合前备份中；当前发布只保留官方固件实施路径。

`scripts/install-agent-dashboard-macos.py` 将当前源码安装到独立运行目录；`scripts/configure-agent-dashboard-firmware.py` 可从本机服务配置生成被 Git 忽略的个人默认头文件。仓库不包含个人令牌、会话记录、设备备份、编译产物或平台虚拟环境。
