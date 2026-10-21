# VibeMate PC 端

负责人：项目负责人

## 范围

- Windows / PC 端 Coding Agent 状态检测。
- VibeMate 产品状态与状态切换逻辑。
- PC 到 CoreS3-SE 的局域网通信。
- 协议测试工具、日志和后续配置界面。

## 第一阶段

先提供一个最小测试发送端，经 Wi-Fi 向 CoreS3-SE 发送：

```text
RUNNING
```

具体传输方式在 `docs/protocol.md` 评审确定后实现。当前不绑定任何 Coding Agent，也不实现完整产品逻辑。

## 后续建议结构

```text
vibemate_pc/
├── src/
├── tests/
└── config/
```

需要 Git 跟踪的空目录应在加入实际代码时再创建。
