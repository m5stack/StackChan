# VibeMate 团队分工

## 职责

| 成员 | 负责范围 | 当前阶段交付 |
| --- | --- | --- |
| 项目负责人 | PC 端 Coding Agent 状态检测、VibeMate 状态协议、产品逻辑 | 定义最小状态协议，并准备 PC 端测试发送程序 |
| A | 硬件电控适配：SCS0009、FE-URT、供电、UART、舵机 ID、接线与稳定性 | 在 PC + FE-URT 环境下独立控制两颗舵机并完成记录 |
| B | 官方 StackChan 结构复现、3D 打印、装配验证、CAD 修改与 VibeMate 外观 | 整理结构来源、打印参数、装配问题和改版记录 |
| C | 基于官方 `firmware/` 的嵌入式开发与系统集成 | 跑通 CoreS3-SE 编译、烧录、屏幕、Wi-Fi 和 `RUNNING` 显示 |

## Git 协作约定

- `main`：稳定主分支，只通过 Pull Request 合入。
- `feature/pc`：PC 端与通信协议开发。
- `feature/hardware`：硬件记录与测试程序。
- `feature/mechanical`：结构文件、打印与装配记录。
- `feature/firmware`：基于官方 `firmware/` 的固件开发。
- 日常开发不直接提交到 `main`；每项功能完成并自测后再发起 Pull Request。
- 保留 `upstream` 跟踪 `m5stack/StackChan`，定期同步官方更新；团队仓库使用 `origin`。
- 不采用额外的 `develop`、release 或 hotfix 分支，保持流程简单。

## 交接约定

- PC 与固件的消息格式记录在 `docs/protocol.md`。
- 电气参数和接线变化记录在 `hardware/README.md`。
- 结构版本、打印参数和装配问题记录在 `mechanical/README.md`。
- 阶段验收结果记录在 `docs/milestones.md`。
