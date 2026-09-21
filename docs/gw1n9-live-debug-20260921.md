# GW1NR-9C Flash 现场诊断（2026-09-21）

后续进展：同日20:16的MCU本地直驱诊断已成功擦写并实际读回一页，64字零差异，擦除reload后Flash Lock清零。见[后续实测记录](gw1n9-direct-result-20260921.md)。本文保留此前v4、openFPGALoader和Python实验的失败结果；文中的“尚未验证”等表述仅指当时状态。

## 结论边界

SRAM 下载原本就正常；本轮同一 LED 文件仍可运行，用户确认 LED 闪烁。此结果不代表内置 Flash 修好。

本轮发现当前固件的连续等待识别没有覆盖本机 openFPGALoader 的 TMS 等待形式。但是，绕开此差异、显式触发连续擦除和每字 Idle 的 Python 探针仍失败。因此没有找到足以独立解释全部现象的根因，也没有依据认定 Flash 损坏。

USB 主机输出未发现短写、传输错误或镜像差异。MCU 抽样页摘要正确。证据到软件 GPIO 执行边界为止，不等同于物理引脚波形或 Flash 读回。

## 环境和复现输入

- 工作树：`/home/dxxdx/back-ft232/CH32_FT232`，分支 `1n9fix`，HEAD `668b64e4fc595e6d8f39ce6e5986dad297bccd70`，有先前修改。
- MCU 实际 ACM banner：`v4 ERASE32 IDLE64 22PINOUT`，串号 `CH32_FTDI_260921113827`，HCLK 144 MHz。
- FPGA IDCODE：`0x1100481B`。MODE[2:0]=000 为用户报告。
- `/usr/bin/openFPGALoader -V`：v0.13.1。
- 文件：`/home/dxxdx/FPGAProjects/9K_LED_project.fs`。
- 与 `/home/dxxdx/gitProj/TangNano-9K-example/led/impl/pnr/9K_LED_project.fs` 完全相同。
- FS SHA256：`7f7da59b6dabd011c751d7f26db1ebd47de11adbe5b47f1c877e99103d4ba06b`，USERCODE/checksum `63BD`。
- 没有在同一块 9C 上确认 BL702 成功，不能把其它板的成功当成本板对照。

## 实测结果

| 操作 | 结果 | 证据 |
|---|---|---|
| openFPGALoader 内置 Flash 完整烧录，两次，其中一次完整 USB 抓包 | 均 CRC FAIL，读 USERCODE=0，最终状态 `39020`；进程仍退出 0 | `ofl-baseline.log`、`ofl-full.log` |
| Python 连续擦除 + 第一页可读标记 | 擦除/编程路径执行，标记未使读回可用，Flash Lock=1 | `marker-continuous/result.json` |
| 分阶段探针，在 reload 前后分别检查 | 擦后 disable 前 `390A0`，disable 后 `39020`；标记写入后及 reload 后仍 Lock=1 | `phase-probe/result.json` |
| Python 同一完整镜像，整页收齐、连续擦除和每字128拍 Idle | 1011页、64704字执行完仍失败，reload 后 `39020`、USERCODE=0 | `continuous-full/result.json` |
| SRAM 加载同一 FS | 状态 `3F020`、USERCODE `63BD`，用户确认 LED 闪烁 | `ofl-sram.log` |

本轮没有获得有效内置 Flash 内容。Python 读回门控返回不可用，不能将其说成“读回全零”。openFPGALoader 的 `Read: 0x00000000` 是 USERCODE 比较，不是整片 Flash 内容，也不是读回 CRC 算法的结果。

## USB 和 MCU 数据核对

`ofl-full.trace` 完整记录 libusb bulk，不是 usbmon 物理总线捕获。`analyze_full.py` 解码 MPSSE/TAP，并按 openFPGALoader 的布局构造预期镜像：`4757314E`、20 字节 FF、FS 二进制内容、末页 FF 填充。

- 完整镜像：1011 页、64,704 个 32 位数据字、258,816 字节。
- 地址均为页号乘 64，每页 64 个数据字；零字节差异。
- 重建输出和预期 SHA256 均为 `d9cbd0677f1a896e60ad8852819155c13bcf750e02e7e063396e93c49a69e9d4`。
- 成功 bulk OUT 总量 3,038,303 字节；未发现短写、USB bulk 错误或截断的抓包记录。
- `analyze_acm.py` 比较对应完整烧录中的 17 个 `PAGE_GPIO` 抽样摘要，全部匹配；页 0 首字为 `4757314E`。
- ACM 中未出现新的 `FAULT` 或 `BAD_SHIFT`。`drop=0x117` 在本轮开始前就存在，本轮未增长；这是诊断事件丢弃计数，不是 USB 丢包计数。

这不证明全部物理 TCK/TDI 边沿正确，也不能用软件滚动摘要代替 FPGA 内容验证。

## 时序差异和负面结果

1. openFPGALoader 的擦除地址 `IR75 + DR32(0)` 已触发 `FUSED_DONE`，恢复的地址融合确实生效。
2. 本机 openFPGALoader 发出 `4B` TMS 命令构成等待；完整烧录期间没有 `ERASE_DONE` 或 `IDLE_DONE`。当前补偿只识别长零字节流以及特定 `4B/7/01 + 19` 每字等待，不能宣称“调大固件擦除拍数已经覆盖 openFPGALoader”。
3. Python 两次实验实际触发准备阶段和 IR75 后各 600,000 拍连续 GPIO 循环。固件时基报告每段约 337.500 ms，按拍数折算约 1.778 MHz。128 拍每字 Idle 为 72.375 us。这些是软件计时，不是示波器测量。
4. 第二次准备阶段显式设置 IR02；同一页 64 个字收齐后执行，仍未获得可用读回。不能只归因于第一轮准备阶段停在 IR13，或只归因于 reload。
5. 当前 DR32、退出到 Idle、连续等待分别调用，之间仍有命令解析和诊断观察开销；尚未验证引脚实际间断是否违反器件要求。诊断代码本身也有耗时，不能因日志位于关中断段就认为无时序影响。

[Gowin UG290](https://cdn.gowinsemi.com.cn/UG290E.pdf) 给出 GW1N(R)-9(C) 的 Flash TCK 范围及准备、擦除、每字编程等待要求。不能把增加拍数简单等同于满足所有时序要求，也没有证据表明本次长等待已造成硬件损伤。

[openFPGALoader v0.13.1 gowin.cpp](https://github.com/trabucayre/openFPGALoader/blob/v0.13.1/src/gowin.cpp) 的 `eraseFLASH()` 根据 DONE=0 结束，未做整片空白读回；`checkCRC()` 打印 FAIL 后不抛出异常。因此 `Erase FLASH DONE` 和退出码 0 均不能用作本例 Flash 成功依据。

## 日志端口单独核对

minicom 抓取中出现部分拼接/截断行。关闭 minicom 后使用原始 ACM 捕获，约 12 秒内的 14 条 COUNTS 全部完整且计数不变。说明此短时间原始读取没有复现截断，不能据此认定 MCU 正在周期性操作 Flash，也不足以直接归因到某一个终端或驱动。

COUNTS 是约 1 Hz 心跳；计数不变时没有额外 JTAG 执行证据。

补充：随后连续完整镜像实验使用原始 ACM 捕获，也出现不完整 COUNTS 行。因此不能把早先截断简单归因于 minicom；短时空闲捕获完整，不代表带负载时 CDC 一定完整。直驱诊断版不链接此观察器。

## 复现及后续对照

全部日志、原始 USB 数据、离线分析脚本及 JSON：

`../logs/ofl-debug-20260921-194433/`

完整 Flash 复现命令（会改写 FPGA 内置 Flash）：

```sh
/usr/bin/openFPGALoader -c ft2232 --ftdi-serial CH32_FTDI_260921113827 \
  --freq 2500000 -f --verbose-level 1 /home/dxxdx/FPGAProjects/9K_LED_project.fs
```

这里 `--freq` 是主机配置；固件忽略 MPSSE `0x86`，不能将软件报告频率当成实测 TCK。连续时钟探针只适配本轮固件，不应直接套用其它串号/固件。

用户确认暂时只有 CH32，无法更换 BL702。完整镜像的连续等待对照也失败，因此准备了 `diagnostics/gw1n9_direct` 独立诊断入口，复用 BL702 风格 GPIO 输出，在 MCU 本地固定执行一页写读；其中还将9C字后等待改为24拍。它是待上板的隔离实验，不是已确认的修复。引脚波形对照尚未进行。

本轮新增诊断脚本、日志、文档和独立直驱固件入口，未刷写 MCU。已关闭本轮 minicom 和 ACM 捕获；完整镜像实验后已恢复 SRAM LED 设计，状态 `3F020`、CRC Success。该恢复只是还原板上运行状态，并非用 SRAM 成功代替 Flash 验证。
