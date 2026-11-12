# gateway/ AGENTS.md (stackchan-mcp)

Python MCP gateway (`stackchan_mcp` package) developer guide.

本项目使用官方 Mooncake 固件；构建与设备连接见 `../docs/agent-tasks-setup.zh-CN.md`。下文 WebSocket 设备说明仅适用于上游替代固件。

## 1. stdio MCP constraint

The gateway runs as a **stdio MCP server** — it lives as long as the host process keeps stdin/stdout open.

- Editing `.env` does NOT reload the running process. **MCP process restart is required** (restart the host application or reconnect).
- Killing the gateway process directly does NOT auto-restart it. The host must reconnect.

### Verifying token configuration

```bash
python3 -c "import os; print('token configured:', bool(os.getenv('STACKCHAN_TOKEN') or os.getenv('BEARER_TOKEN')))"
```

## 2. `STACKCHAN_TOKEN` vs `BEARER_TOKEN` priority

```python
expected = os.getenv("STACKCHAN_TOKEN") or os.getenv("BEARER_TOKEN")
```

**`STACKCHAN_TOKEN` takes priority**; `BEARER_TOKEN` is a legacy alias checked only when the primary is empty. Keep both set to the same value as insurance.

官方看板在 SETUP → Apps → Agent 看板中设置相同令牌。

## 3. Gateway status check

```bash
# Process check
lsof -i :8765 | grep LISTEN          # python LISTEN = OK
pgrep -fl stackchan_mcp              # process list

# Port conflict check
lsof -i :8765                        # ESTABLISHED sockets are MCP client connections, not conflicts
```

If `address already in use ('0.0.0.0', 8765)` occurs, identify the existing process with `lsof` before killing.

## 4. LAN IP changes (gateway side)

The gateway listens on `0.0.0.0:8765`, so LAN IP changes do not directly affect it. The issue is on the ESP32 side — 见 `../docs/agent-tasks-setup.zh-CN.md`。

## 5. Validation commands (pre-PR)

```bash
cd gateway
uv sync                 # resolve dependencies
uv run pytest           # tests must pass
uv run ruff check .     # lint must pass
```

CI runs the same three commands. A local failure means CI will also fail.

## 6. PyPI publishing

- Bump `version` in `gateway/pyproject.toml`
- Promote `CHANGELOG.md` `[Unreleased]` Gateway subsection to `[X.Y.Z] - YYYY-MM-DD`
- Tag push (`git tag vX.Y.Z && git push origin vX.Y.Z`) triggers Trusted Publishing
- Verify with `pipx install --force stackchan-mcp` after publishing

## 7. `.env` edit checklist

```bash
# 1. Check that a token is set without printing it
python3 -c "import os; print('token configured:', bool(os.getenv('STACKCHAN_TOKEN') or os.getenv('BEARER_TOKEN')))"

# 2. Restart MCP process (host restart or /mcp reconnect)

# 3. Verify reconnection
# In host: get_status → connected: true / tools_count: 14+

# 4. If token mismatch, also update ESP32 sdkconfig
#    见 ../docs/agent-tasks-setup.zh-CN.md
```
