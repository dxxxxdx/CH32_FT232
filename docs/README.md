# 文档索引

使用和构建从[项目 README](../readme.md)开始。本次发布说明整理于 2026-09-22，Flash 修复实测发生于 2026-09-21。

## 当前使用与实现

| 文档 | 内容 |
| --- | --- |
| [更新说明](../UPDATE_2026-09-21.md) | Flash 修复、配置重构、旧配置迁移与验证范围 |
| [PinSelectGUI](../PinSelectGUI/README.md) | 图形选择、保存配置和构建流程 |
| [板级配置](../boardtype/README.md) | 配置文件、硬件宏传递和新增板型 |
| [JTAG 内核](../jtag_core/jtagManager.md) | 缓冲、批次、连续 DR32 和已知限制 |
| [USB 接口](../usb_bsp/ft232Descriptor.md) | FTDI/CDC 枚举、控制请求和端点交接 |

## Flash 排查归档

以下按排查顺序保留，正文中的“当前”“候选版”“尚未验证”指各次记录当时的状态。旧路径、宏和构建命令不适用于当前版本；当前构建流程以上面的使用文档为准。

| 记录 | 阶段结论 |
| --- | --- |
| [BL702 静态对照](1n9fix-bl702-static-audit-20260921.md) | 比较历史基线的缓冲与时序，未进行运行验证 |
| [现场诊断](gw1n9-live-debug-20260921.md) | SRAM 正常，Flash 失败；软件发送日志不能证明写入成功 |
| [MCU 本地直驱](gw1n9-direct-result-20260921.md) | 本地直驱单页实际读回成功，交付 LOCAL_IDLE 候选版 |
| [LOCAL_IDLE 实测](gw1n9-normal-result-20260921.md) | 正常 MPSSE 路径仍失败，交付 ATOMIC_COMMIT 候选版 |
| [ATOMIC_COMMIT 实测](gw1n9-atomic-result-20260921.md) | 单页仍失败，进一步暂存进入 Shift-DR 序列 |
| [DR_TRANSACTION 成功记录](gw1n9-flash-success-20260921.md) | 单页 64 字、完整 Flash 烧录、独立重新加载通过；用户确认冷启动 |

成功记录对应特定固件，后续配置重构和不同构建不能直接继承其板测结论。归档中提及的 `logs/`、`host_tools/`、独立诊断固件和旧二进制路径是开发机本地材料，源码包不保证包含；因此这些路径以文本保留，不作为下载链接。
