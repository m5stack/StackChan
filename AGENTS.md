# StackChan 官方固件与 Agent 看板

本项目以 `m5stack/StackChan` 为固件基础。`firmware/` 保持官方 ESP-IDF 6.1、Mooncake、HAL 与 Setup 架构；`gateway/` 是来自 `kisaragi-mochi/stackchan-mcp` 的独立 Python 进程，保留上游许可证。

## 架构与检查

- Codex/Claude 适配器经 MCP 写入网关快照；设备每秒通过 Wi-Fi HTTP 读取 `/agent-dashboard`。
- 网关其它 WebSocket 设备控制功能依赖其上游替代固件，不代表本项目官方固件支持这些功能。
- 网关检查：`cd gateway && uv run --locked pytest && uv run --locked ruff check .`。
- 固件宿主检查：`cmake -S firmware/tests -B firmware/build-host-tests && cmake --build firmware/build-host-tests && ctest --test-dir firmware/build-host-tests --output-on-failure`。
- 固件构建、设备配置及已验收范围见 `docs/agent-tasks-setup.zh-CN.md`、`docs/agent-tasks-validation.zh-CN.md`。
- 面向单用户家庭局域网，优先验证实机主要路径；审查建议应友好，样式问题标为非阻断。
- 不提交 `.env`、令牌、本机默认头文件、会话数据、设备备份或构建产物；日志不输出令牌或聊天正文。

## StackChan 待办同步

本机已安装 Agent 看板时，Codex 主会话在制定、调整、完成工作计划时同步短标题与状态。使用 `~/.local/share/stackchan-agent-dashboard/.venv/bin/python -m stackchan_mcp.agent_adapters.runner codex-plan`，通过 stdin 提交 JSON：`{"stage":"实现","plan":[{"step":"短任务标题","status":"in_progress"}]}`。会话 ID 使用 `CODEX_THREAD_ID`，最多 12 项，状态为 `pending`、`in_progress`、`completed`；缺少命令或会话 ID 时继续工作，不伪造 ID。

监视器负责合并计划与生命周期后经 MCP 上报；不要同时用完整快照覆盖同栏。子 Agent 不提交主栏计划。只提交短标题和状态，不上传提示词、正文、路径、工具参数或推理内容。
