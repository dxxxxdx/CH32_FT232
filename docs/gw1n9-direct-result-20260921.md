# GW1NR-9C 内置Flash单页成功与正常固件候选版

> 历史调试记录：正文中的状态、路径与候选版名称均指记录当时。当前用法见[项目 README](../readme.md)，最终 Flash 修复结果见[成功记录](gw1n9-flash-success-20260921.md)。本地日志、工具和旧固件路径不作为发布附件。

后续20:37：本文LOCAL_IDLE正常版上板仍失败；直接诊断单页成功结论不变。当前ATOMIC_COMMIT候选版及失败实测见[后续记录](gw1n9-normal-result-20260921.md)。

2026-09-21 20:16，用户手动刷入直驱诊断固件后，当前9C板完成一次整片擦除、第一页写入和实际读回。固件比较64字零差异，主机取回64个原始数值再比较也为零差异。没有重复擦写或自动重试。

## 实测证据

- MCU串号：`CH32_FTDI_D60921120824`；FPGA ID：`0x1100481B`。
- 用户刷入：`firmware-22pinout-gw1n9-direct-20260921-2008/CH32_GW1N9_DIRECT_22PINOUT.bin`。
- 命令：`python3 -B host_tools/gw1n9_direct/probe.py --serial CH32_FTDI_D60921120824 --run-one-page`。
- 结果：`VERIFIED`，`run_count=1`，`words_compared=64`，`mismatch_count=0`，`host_mismatch_indices=[]`。
- 总时基计数194875628，HCLK144MHz，约1.353秒，包含两次500ms reload等待。
- 原始记录：run-one-page.jsonl（本地归档：`logs/gw1n9-direct-20260921-201619/run-one-page.jsonl`）。同目录保存运行前检查、运行后取回、stderr和命令清单。

| 阶段 | STATUS | USERCODE |
| --- | --- | --- |
| 执行前 | 00039020 | 00000000 |
| SRAM擦除后 | 00039020 | 00000000 |
| Flash擦除并reload后，尚未写页 | 00019020 | 00000000 |
| 写页并reload后 | 00019020 | 00000000 |
| 实际读回64字后 | 00019020 | 00000000 |

Flash Lock（bit17）在擦除并reload后已经清零。这不是由写入首字可读标记后才出现的变化。最终读回包含`F7F73F4F`、`00000000`、`FFFFFFFF`、`01234567`、`89ABCDEF`、walking-one及反相图样，排除了仅凭全零/全FF判断成功的情况。

后续`--results`再次读取当前状态仍为19020，并取回同一份保存结果；它没有再次读取Flash，也没有再次擦写。执行后Flash仅有诊断第一页，不是完整LED设计，USERCODE=0在该诊断图样下不是失败证据。

## 能得出的结论

这块9C的内置Flash以及当前CH32至FPGA的连线能够完成一次真实擦写读回。此前正常路径的39020、USERCODE=0不能直接归因于Flash损坏或永久锁定。失败路径与成功路径在擦除后就已经出现状态差异。

直驱同时改变了准备和等待组织、每字等待拍数，并移除了MPSSE解析、USB取数和逐字ACM观察。它不是单变量实验，不能仅凭此结果认定某一个USB空包或某一段延迟就是唯一根因。也没有同板BL702对照。

## 正常MPSSE候选版

交付目录：`firmware-22pinout-local-idle-20260921-2026/`，串号`CH32_FTDI_260921122636`。

保留RX4096、TX1024和BL702批次收包策略，固定22pinout。由现有Gowin内核维护准备、退出和等待状态，不新增USB页缓冲或第二份控制TAP。

1. IR09之后，在后续Idle进入IR的转换前补1200拍准备窗口，直到15/75/17/05或TAP reset清标志。
2. IR75首个地址DR32结束并返回Idle后，在当前调用立即执行600000拍擦除等待。
3. IR71地址DR32返回Idle后等待32拍，后续每个数据字等待24拍。识别OFL两拍和Gowin七拍返回形式；七拍中的额外Idle并入本地窗口。
4. 已由本地窗口覆盖的后续纯Idle请求只消费，避免再次叠加主机等待。普通读回、其它TAP转换仍执行。
5. 交付构建关闭ACM观察及UART转发。CMake排除日志、交付目录、host_tools及独立诊断入口，防止递归源文件收集混入备份或另一main。

在相同GPIO循环的软件计时折算下，1200/32/24拍约为675/18/13.5微秒，600000拍约337.5毫秒；不是示波器测量。与成功直驱版的214字节DR32及156字节Run-Test机器码逐字节相同，见比较记录（本地归档：`logs/gw1n9-direct-20260921-201619/normal-gpio-code-comparison.json`）。

编译通过：bin16100字节，RAM7668字节（含2048字节预留栈）。`git diff --check`通过。遵照用户要求，没有运行本地测试套件。保留原根目录ELF，交付使用独立构建目录产物。

限制：这仍是主机MPSSE路径，DR32尾拍和退出命令可能跨USB包，准备窗口后到IR15也仍有解析开销。因此底层循环机器码一致并不等于整段波形完全一致。完整`.fs`烧录、USERCODE校验及断电启动尚未验证。

用户手动刷入候选版后，先核对USB串号，再用同一`/home/dxxdx/FPGAProjects/9K_LED_project.fs`进行内置Flash烧录。当前直驱版不能接收openFPGALoader命令；须先更换固件。正常候选版不提供ACM日志。
