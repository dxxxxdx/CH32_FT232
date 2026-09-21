# GW1N(R)-9C 正常 MPSSE 内置 Flash 烧录成功记录

> 本文记录特定固件的硬件结果。后续板级配置、GUI 与构建流程改动不自动继承该验证结论；当前使用说明见[项目 README](../readme.md)。

2026-09-21 20:58，用户手动刷入DR_TRANSACTION固件后，在同一9C板、同一CH32和同一LED文件上完成单页实际读回、openFPGALoader完整Flash烧录及独立重新加载检查。

## 已验证固件与输入

- 当时分支为 `1n9fix`，测试的是当时工作区构建的 DR_TRANSACTION 固件，以串号和校验值标识。
- MCU USB串号`CH32_FTDI_260921125322`，22PINOUT 接线，ACM 调试及 UART 转发关闭。
- 历史 BIN 路径：`firmware-22pinout-dr-transaction-20260921-2053/CH32_FT232_22PINOUT_DR_TRANSACTION.bin`。
- BIN SHA256：`201bafbdef2b5c5326f15daea21bec1c2c56a91897c63ddc0c1006743bf5e017`。
- FPGA原始IDCODE：`1100481B`，openFPGALoader显示型号GW1N(R)-9C。
- 文件：`9K_LED_project.fs`，SHA256 `7f7da59b6dabd011c751d7f26db1ebd47de11adbe5b47f1c877e99103d4ba06b`，校验值63BD。
- 原始日志：`logs/gw1n9-dr-transaction-20260921-205822/`。

历史 BIN 归档目录已不在当前工作区；上述路径和原始日志路径用于追溯，不表示发布包包含这些附件。

## 硬件结果

| 检查 | 结果 | 原始记录 |
| --- | --- | --- |
| 单页擦除、64字图样写入、实际TDO读回 | verified=true，64字零差异 | single-page/result.json、actual-word32.raw/bin/tsv |
| 擦除后状态 | 39020变为19020，Flash Lock清零 | single-page/trace.jsonl |
| 同一完整LED文件烧入内置Flash | `CRC check: Success`，最终状态1F020、DONE=1 | ofl-flash.log |
| 重新打开USB，独立读取状态及USERCODE | 状态1F020，USERCODE=000063BD | reload-check/trace.jsonl |
| 只发3C/02重新从Flash加载，再读寄存器 | 仍为1F020、USERCODE=000063BD | reload-check/result.json |
| FPGA完全断电后重新上电 | LED仍闪烁，冷启动通过 | 用户现场反馈 |

独立重新加载步骤没有发送SRAM镜像、Flash擦除或编程命令。因此此处成功不是先前正常的SRAM下载结果。单页读回包含F7F73F4F、01234567、89ABCDEF、walking-one及反相图样，既检查了数据也检查了位序。

完整FS的校验是openFPGALoader v0.13.1对USERCODE/checksum的比较，并非整片逐字节读回。退出0不是单独的成功依据；本轮有明确的CRC Success和独立寄存器结果。

## 修正与证据边界

上一版ATOMIC_COMMIT使用相同缓冲、等待拍数及底层GPIO循环，同一单页脚本仍失败。当前版增加暂存进入Shift-DR的三拍，只有整字和退出后缀齐全后才开始物理DR事务：进入、32位数据、两拍Update/Idle、连续等待。

这个变化消除了提前进入Shift-DR后，等待缓存/解析24+7+1数据和退出命令的间隙。两轮擦除前缀都在单个USB包内，因此不能把现象简单说成USB丢包或仅是跨包间隔。证据支持完整DR事务连续执行是本次起效的关键；没有仪器波形，未测得允许的具体最长间隙，也不推断Flash内部模拟电路机理。

RX仍4096字节、TX1024字节，BL702批次策略不变；准备/擦除/地址/字后拍数仍1200/600000/32/24。未尝试恢复所有历史改动来证明哪些是最低必要条件；没有同板BL702对照。

## 验证边界

本次记录结束时，FPGA 内置 Flash 已写入 LED 设计，独立 reload 状态及校验通过；本文不描述设备此后的固件或 Flash 内容。

用户随后明确确认：“已断电重上电，LED 仍闪烁”。冷启动观察通过，证据来源为用户现场反馈；未将软件reload冒充断电测试。这份已验证固件的 Gowin Programmer GUI 路径未重测，不能由openFPGALoader成功直接宣称GUI也已验证。

本次实测范围为 FPGA 内置 Flash；MCU 固件由用户手动刷入，外置 Flash 未纳入测试。
