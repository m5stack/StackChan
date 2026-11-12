# StackChan Agent 待办看板设计

## 目标

在 M5Stack 官方 StackChan 固件中新增一个 Mooncake 应用，在 320×240 屏幕上查看 Codex 和 Claude 的状态、阶段及待办。左栏固定为 Codex，右栏固定为 Claude。Agent 通过 `stackchan-mcp` 的 MCP 服务提交状态；设备离线时保留最近任务并显示离线状态。头像和原语音交互不变。

固件以 [M5Stack/StackChan](https://github.com/m5stack/StackChan) 为基线。`stackchan-mcp` 继续作为独立网关和 MCP 状态服务，不把其 fork 固件作为设备固件。

## 架构

```text
Codex / Claude adapters
        │ MCP Streamable HTTP
        ▼
stackchan-mcp gateway ── AgentStatusStore
        ▲                       │
        │ HTTP GET              └── latest two-agent snapshot
        │ (1 second polling)
M5Stack official StackChan
  Mooncake Agent Tasks app
```

固件通过新增的独立 Mooncake App 显示看板，并由官方 Launcher 展示其应用图标。Agent 不连接设备。设备读取状态使用网关的只读 HTTP 接口；MCP 仍是状态写入入口。该边界适配官方固件当前的应用及联网结构，也避免依赖 `stackchan-mcp` fork 内的设备 WebSocket MCP 协议。

## 网关接口

- `agent_update_snapshot` 接受单个 Agent 的完整快照，沿用 `AgentStatusStore` 的会话隔离、序号拒绝、文本长度限制及 45 秒租约。
- `agent_get_dashboard` 返回两个 Agent 的最新完整视图，顺序固定为 Codex、Claude。
- 新增受现有 Bearer token 保护的 `GET /agent-dashboard`，供固件轮询同一只读视图。响应不包含 prompt、文件路径、工具参数、消息正文或模型推理内容。
- 不新增设备 WebSocket 推送、设备队列 publisher 或 MCP 导航工具。设备网络中断时保留缓存视图；恢复后下一次轮询自动取得最新快照。
- JSON 响应字段为 `agents[]`，每项包含 `agent`、`online`、`stale`、`status`、`stage`、`tasks[]`。状态值为 `idle`、`working`、`waiting`、`completed`、`failed`；任务状态值为 `pending`、`in_progress`、`completed`。

## 官方固件应用

- 在 `firmware/main/apps` 新增 Agent Tasks Mooncake App，并在 `apps.h`、`main.cpp` 注册；Launcher 提供独立入口和 Agent 图标。
- UI 采用深色、高对比、细分隔线的双栏布局。每栏显示图形 Agent 标识、名称、中文状态、短阶段和任务。每栏同时最多显示三条任务，任务较多时在本栏独立滚动。阶段和任务文字进行长度限制。
- 页面提供返回 Launcher 的官方 HomeIndicator；不在头像 App 上拦截触摸，也不更改音频、头部触摸或现有页面手势。
- LVGL 创建、更新和销毁遵循官方 `LvglLockGuard`。HTTP 请求运行在独立 worker，不在 UI 更新或持锁区间执行阻塞网络操作。网络恢复后由 worker 将最新完整快照交给 App，随后在 UI 生命周期中更新 LVGL。
- 空清单显示“暂无待办任务”。来源离线或租约过期时保留最后任务，并标示“离线”或“已过期”。设备暂时无法连接网关时显示上一次数据，不伪造进度。

## 设备配置

在官方 Setup App 中增加 Agent 看板设置页，包含网关只读接口地址和 Bearer token。配置保存到设备 NVS；token 输入默认隐藏，日志不打印 token。设置页提供软键盘、连接测试和保存/取消操作。通用源码默认地址为空；可用本机脚本生成被 Git 忽略的默认地址和令牌，首次加载时回填空字段并持久化，已有非空值优先。未配置地址时看板显示配置提示，不发起外部请求。

设备以约 1 秒间隔轮询 `GET /agent-dashboard`，单次 HTTP 操作超时为 5 秒，响应体上限为 8 KiB，并不跟随 HTTP 重定向，避免 Bearer token 被转发到其他地址。网关返回带 `Content-Length` 的 JSON 响应。地址支持家庭 LAN 的 HTTP 和 HTTPS。一次失败不清空缓存；重复失败显示连接异常状态。

## 事件适配器边界

- Codex Desktop 使用本机实验 rollout 监视器，仅接收 `originator` 明确为 `Codex Desktop` 的主会话，读取回合开始、完成和中断事件。已观察真实事件，但它不是稳定的官方桌面事件订阅 API。
- Codex 待办使用主会话显式 `codex-plan` 入口，同会话计划与生命周期由同一个监视器合并。未观察到桌面原生计划事件时，不从消息正文猜测任务。
- Claude Code 使用本地 transcript 增量监视器，成功的 TaskCreate、TaskUpdate、TodoWrite 更新清单，结束回合保留任务；不依赖全局 Hook。既有会话回放已验证，新模型会话实时验收待完成。
- 只展示最近活动的主会话，不混入子 Agent、其它会话或聊天正文。一个 Agent 栏只使用一个上报来源。
- 每栏稳定排序为“进行中 → 待办 → 已完成”；在线工作时显示转圈、省略号和任务指示动画，完成或离线时停止。

## 验证和验收

- 网关测试覆盖 MCP 更新、旧序号拒绝、双 Agent 输出顺序、45 秒过期、HTTP Bearer 授权、错误输入和不泄露额外字段。
- 固件 host tests 覆盖两列映射、状态标签、UTF-8 截断、空清单、离线保留任务及非法快照拒绝。
- 用官方仓库文档所列 ESP-IDF v6.1 流程构建；确认新增 App、Setup 配置和依赖不破坏官方 Launcher 与现有应用。
- 在设备上检查 320×240 双栏排版、滚动、返回、网络断开/恢复、状态更新，以及 Avatar、语音触摸和头部手势未受影响。
- 刷写只使用官方固件仓库的标准 CoreS3/StackChan 指引。先确认生成镜像和目标板型；下载模式由 USB 完成刷写，BLE 用于启动后广播发现，不作为刷写通道。

## 实施顺序

1. 在 `stackchan-mcp` 中完成并验证 MCP 状态工具及只读 HTTP 快照接口。
2. 在官方固件中新增 App、纯数据模型、设置页配置持久化及异步轮询。
3. 完成网关/固件契约测试、官方 ESP-IDF 构建和实体设备验收。
4. 按真实生命周期与任务事件证据启用本机实验适配器，明确记录其限制与尚未验收的实机行为。
