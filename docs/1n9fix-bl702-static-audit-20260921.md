实现更新（2026-09-21）：工作区 CMake 已固定 22PINOUT，RX/TX 为 4096/1024 B；随后按用户要求移植了 BL702 的收集、批处理、回包和 JTAG 行为。当前实现及保留行为见 [JTAG 内核说明](../jtag_core/jtagManager.md)。仅静态核对，未编译或运行验证。

下面完整保留修改前 `668b64e` 的审查快照，描述的是当时的源码，不代表当前工作区。

本次静态对照发现：当前 RX 容量与 BL702 相同，但编程页的收齐条件、TX 容量、DR32 边沿延时和擦除时钟生成方式仍有明显差异。最先需要审查的是 2 ms 提前执行；如果仍使用之前的 22PINOUT 硬件，还必须先处理当前分支强制 20PINOUT 的构建配置。

审查日期：2026-09-21。本文只记录源码事实、人工控制流推导和待验证的时序风险，没有运行固件、回放程序、测试、编译器或硬件工具，也没有修改固件源码。

**审查对象与结论边界**

- CH32：`/home/dxxdx/back-ft232/CH32_FT232`，当前分支 `1n9fix`，HEAD 与 master 均为 `668b64e4fc595e6d8f39ce6e5986dad297bccd70`。被审查的 C/H、CMake 和链接脚本没有工作区修改。
- BL702：`/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702`，分支 `main`，HEAD 为 `1b27d3f74dfc7cfb157e6560ca8a3e7d964b391a`，工作区干净。
- BL702 的 [CMakeLists.txt](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/CMakeLists.txt:3) 指向 `jtag_process.c`；旁边的 `jtag_process.c.gowin` 没有列入该目标。本次按源码内默认开启的 DR32 合并、整批处理及编程页收集宏分析，不声称核实了某块 BL702 板上二进制的编译参数。
- 根目录 `CH32_FT232.elf` 有用户修改，另有上一分支遗留的构建/固件目录。本文不把这些二进制视为当前源码的构建结果，也不判断设备当前实际运行哪版固件。
- 上次 8 KiB、空状态包门控、16/22 µs 编程窗口及降速改动不属于当前源码。此前候选固件的失败结果不能直接等同于本分支的测试结果。
- 下文优先级指审查/修复顺序。确定存在代码差异或条件性缺陷，不等于已经证明它造成这块 9C 的失败。

**构建前置问题：仍接 22PINOUT 时，当前板型会选错**

[CMakeLists.txt:61](/home/dxxdx/back-ft232/CH32_FT232/CMakeLists.txt:61) 用 `CACHE ... FORCE` 固定 `BOARD_TYPE=20PINOUT`。即使传入 `-DBOARD_TYPE=22PINOUT`，也会被覆盖；当前分支没有 `boardtype/22pinout.h`。

| 引脚 | 当前 20PINOUT 源码 | 此前会话中使用的 22PINOUT |
| --- | --- | --- |
| TCK | PA6 | PA6 |
| TMS | PA5 | PA7 |
| TDI | PA4 | PA5 |
| TDO | PA2 | PA4 |

依据：[20pinout.h:17](/home/dxxdx/back-ft232/CH32_FT232/boardtype/20pinout.h:17)。22PINOUT 列来自此前已核实的板级配置，并非当前分支文件。

如果硬件仍是此前那块板，直接构建当前分支会错接三个 JTAG 信号。这能导致基础 JTAG 就不工作；它不能解释已经正常读 ID、正常烧 SRAM 的旧固件为何只在 Flash 上失败。如果现在换成了 20PINOUT 板，则此项不适用。当前还默认启用运行灯，20PINOUT 灯脚是 PA15，需要随板型一起核对。

**容量与边界核对**

| 项目 | CH32 当前分支 | BL702 对照源码 | 判断 |
| --- | --- | --- | --- |
| JTAG RX | 4096 B | 4096 B | 容量相同，不能只凭容量认定 CH32 又不够 |
| JTAG TX | 512 B | 1024 B | CH32 更早触发读回背压，见第 3 项 |
| USB OUT 包 | 最多 64 B | 最多 64 B | USB 包不是一页 Flash，也不是整个 FS 文件 |
| USB IN 有效负载 | 每包最多 62 B，另加 `31 60` | 同样保留 2 B 状态头 | 不能为了消除空包而删除数据包状态头 |
| RX 空间不足 | 整包拒绝搬入，保留 USB 邮箱，先执行已有批次 | 检查剩余空间，转 ready 后先执行 | 未发现当前 CH32 正常收包路径直接覆盖未执行数据 |
| 环形队列容量 | 单独维护 `used`，可区分空与满 | RX 为线性缓冲 | CH32 没有少用一个槽位导致 4096 变 4095 的问题 |

依据：[CH32 容量定义](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagManager.h:12)、[整包背压](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagManagerBuffer.c:19)、[环形队列计数](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagRingBuffer.c:44)、[USB OUT 邮箱](/home/dxxdx/back-ft232/CH32_FT232/usb_bsp/ft232Usbd.c:1304)、[BL702 缓冲区](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:90)。

BL702 注释描述的编程传输是 1754 B，只能说明其设计针对的一个传输形态，不是所有 Programmer/文件的最大长度保证。若连续传输确实超过 4096 B，两边仍会在容量边界分批；不丢字节并不自动保证 Flash 所需的连续时序。

当前 [Link.ld](/home/dxxdx/back-ft232/CH32_FT232/Ld/Link.ld:3) 预留 2048 B 栈，总 RAM 为 10240 B。直接把 RX 改为 8192 而不重新安排内存，仅 `8192 + 512 + 2048 = 10752 B` 就已经超出 RAM，尚未计算 USB、UART 和其它状态。不能把上一分支的 8 KiB 定义单独搬过来，也不能未经依据就削减栈。

**1. 优先级高：2 ms 超时会把未收齐的编程页提前执行**

CH32 所有输入共用同一个批次策略：短包、空间不足 64 B、或距离上一包满 2 ms 即执行。[ftdiJtagService.c:254](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/ftdiJtagService.c:254)、[超时放行位置](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/ftdiJtagService.c:270)。

BL702 在首包前 32 B 匹配 `4B 03 03 1B 06 71`，识别编程传输后保持 collecting，直到非零短包或容量边界。`jtag_process()` 在 collecting 时直接返回，不存在 2 ms 提前放行。[BL702 收包](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:328)、[处理入口](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:415)。

人工推导的触发条件：同一编程传输先到几个 64 B 满包，后续包因主机调度停顿超过 2 ms。CH32 会关中断执行前缀，耗尽输入后恢复主循环，等剩余部分到达再执行。即使整页只有约 1.7 KiB，4 KiB 足够容纳，仍会被拆开。

这是确定存在的调度差异，也是最值得先修的连续性风险。当前 DR32 暂存可以保护单个字的 24+7+1 拍，并不能保护一整页内所有字、退出和 Idle 的连续关系。增加 RX 不能消除这个超时入口。

修复方向：普通流量和已识别编程传输区分调度；编程收集期不能仅凭短暂空闲执行。还应明确满容量、短包、reset/purge 和整 64 B 结尾的处理，不引入第二套页缓冲或重复 TAP 状态。

**2. 优先级高：DR32 边沿延时与擦除波形并未对齐 BL702**

CH32 的 [jtag_gpio_edge_delay](/home/dxxdx/back-ft232/CH32_FT232/gpio_toggle/GPIO_Cfg.c:410) 是两条 `nop`。BL702 的 [同类延时宏](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:47) 是两轮 `volatile uint32_t` 计数循环，每轮还有 `nop`。计数、比较、分支及变量访问都会产生额外开销，两个“2”不代表相同的半周期。

编程 DR32 两边都采用单端口整字写，在最后一拍带 TMS 离开 Shift-DR，数据位顺序的基本结构相符；最明确的未对齐项是引脚间隔和处理开销。[CH32 DR32](/home/dxxdx/back-ft232/CH32_FT232/gpio_toggle/GPIO_Cfg.c:315)、[BL702 DR32](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:177)。

擦除也不同：

- CH32：在满足 IR75、DR 退出和 Idle 条件后，按 HCLK 时间连续产生 180 ms 时钟；循环中读取时基，拍数不固定；一轮擦除只补一次窗口。另有 SRAM 擦除后的 600 µs 准备窗口。
- BL702：对至少 8000 B、非读、首字节为零的长命令替换为 150000 拍；没有等价 TAP/IR 判断，也没有 CH32 的 600 µs 状态链。其总时长由 GPIO 循环的实际速度决定。

依据：[CH32 擦除识别](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagGowinFlash.c:96)、[CH32 时钟生成](/home/dxxdx/back-ft232/CH32_FT232/gpio_toggle/GPIO_Cfg.c:367)、[BL702 长时钟](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:219)。

这意味着相同 MPSSE 输入不会产生相同擦除/编程波形。但静态审查不能给出两者的真实 MHz、脉宽，也不能据此判定 180 ms 本身错误或 150000 拍必然正确。普通移位、合并 DR32、连续 Idle 是不同热路径，不应把 README 的某次“2 MHz”测量推广到全部路径。

两边都消费但不实际执行 `0x86` 分频，这是共同限制，不是单独解释 CH32 失败的差异。后续时序修改应分别考虑低电平建立、高电平保持、字后/页尾 Idle；仅修改界面频率或照搬 NOP 数字不能证明波形一致。

**3. 优先级中：TX 只有 512 B，长读回会在批次中途恢复中断**

CH32 在 TX 无空位时返回 `JTAG_SERVICE_WAIT_TX`，service 随即解锁，再通过 USB 发出部分结果后继续处理。[jtagManager.c:299](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagManager.c:299)、[service 临界区](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/ftdiJtagService.c:122)。

例如，空 TX 下收到一条 `39 FF 02` 加 768 B 数据的读写命令，该输入小于 4 KiB，但输出 768 B 超过 512 B。依据源码，CH32 不能在一次不间断处理内产生全部回复，必然要等待发送；BL702 的 1 KiB TX 设计可容纳这一数量，且处理批次期间禁止 IN 回调从队列取数据。

这是确定的容量/调度差异，不是已证明的越界：CH32 在取数据和翻转 GPIO 前检查空间，背压本身用于避免丢回复。是否影响当前 Flash 校验，取决于实际读回命令长度和该阶段是否容许暂停；不能把这一项直接套到所有小于 512 B 的校验读取。

修复方向：先把 TX 的容量与批次最大回复需求一起定义，再决定是否对齐 1 KiB。不能只照搬 BL702 的“整批禁止发送”，否则 CH32 可能在 TX 满且不允许发送的条件下无法推进。

**4. 优先级中：完整的退出命令会被消费，却等待下一命令才真正输出**

CH32 在 IR71、物理 TAP 位于 Exit1-DR 时，会暂存完整的两拍 `4B 01 01` 或 `4B 01 81`，不立即输出 Update-DR/Idle。它只在收到后续命令、后续 TMS 数据、或输出屏障时补发。[DeferProgramExit](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagGowinFlash.c:291)、[消费但不输出的分支](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagManager.c:310)、[后续命令补发](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagManager.c:103)。

如果主机本次传输就结束在该完整退出命令，之后没有新命令，RX 已空，批次调度也清空，但物理 TAP 仍停在 Exit1-DR。重复轮询不能完成它，因为 manager 的主循环只在有输入时运行。BL702 对这两拍直接输出，没有这个延后状态。

这是一个有明确触发条件的完成语义问题。当前实现的意图是把退出与后续 Idle 合并，减少中间停顿；但它把后续输入变成了已完整命令完成的前提。尚无本轮数据证明当前 Programmer 恰好在此处结束传输。

修复方向：建立明确的“批次结束后无未履行完整命令”规则；在真正边界处理已完整的退出，不能把恢复任意 2 ms 拆页当作解决方法。

**5. 优先级中：DR32 优化把其它合法命令形式变成终止错误**

在 IR71（也扩展到 IR75 地址）且处于 Shift-DR 时，CH32 一看到长度为 3 的 `0x11/0x19` 就先缓存 24 位、不输出。随后严格要求七位 `0x13/0x1B` 和一位 `0x4B`；中间其它命令会把解析器置为 `JTAG_MPSSE_FAULT`。[ProgramCommandValid](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagGowinFlash.c:163)、[数据约束](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/jtagGowinFlash.c:193)。

人工推导例子（未执行）：在上述 IR/TAP 前提下，输入 `11 02 00 AA BB CC 87 13 06 DD 4B 00 01`。`0x87` 本来是无移位的 Send Immediate，BL702 直接消费它，继续保留 DR32 暂存；CH32 却在 `stage=3` 时把它判为错误，从此不再推进，直至 reset/host TX purge。

这说明当前优化的适用输入比一般 MPSSE 接口窄，不能只凭“位序一样”认为兼容。同样地，主机改变位拆分、插入允许的无时钟控制命令时也需要审查。是否解释当前 Gowin 写页失败仍取决于主机实际发出的形态；本轮没有运行抓包。

修复方向：允许不改变移位含义的命令，或在形态不匹配时按正确顺序补出暂存位并退回普通移位。不能简单清零暂存、跳过错误继续，否则会真正少发时钟/数据。

**6. 优先级较低：空状态包的门控和定时不同，但不能直接归因为 Flash 被写坏**

CH32 在每轮 service 尾部无条件调用 USB 空闲服务；该服务根据 pending、bit mode、IN 端点空闲及固定 7200000 ticks 判断是否补 `31 60`，不认识 JTAG 批次或未履行退出。[service:172](/home/dxxdx/back-ft232/CH32_FT232/jtag_core/ftdiJtagService.c:172)、[USB 条件](/home/dxxdx/back-ft232/CH32_FT232/usb_bsp/ft232Usbd.c:274)。

BL702 只有 `jtag_received_flag` 清零后才进入 IN 发送服务。其 JTAG 空包判断是距上次真实数据发送超过 1000 µs；发送空包时不更新 `last_send`，所以不能表述成严格“每 1 ms 一包”。[BL702 门控](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:401)、[BL702 定时](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/main.c:196)。

特别注意：本分支通常已经在 2 ms 时执行收到的前缀，不能沿用上一版“持续 collecting 超过 50 ms”作为本版常见触发场景。尚未完整接收的命令、延后退出等状态仍可能比空包定时活得更久。

CH32 已有 OUT 到达撤回未发送空包、真实回复取消 pending 的逻辑；空包发送函数本身也不翻转 JTAG 引脚。应作为主机读回兼容和完成语义审查项，不应仅凭 `31 60` 的存在推断写坏 Flash。

**对照基准也有缺陷，不能整份照搬**

BL702 的 `suppress_gpio` 在 `jtag_process()` 入口只初始化一次，第一次暂存编程数据后置 true；`goto process_next_mpsse_byte` 循环没有逐次清零。如果同一批次在合并过一个 DR32 后，又接着出现不需合并的普通 MSB 移位，后面的 `if (!suppress_gpio)` 仍可能跳过 GPIO；MSB 读回路径还可能写入零回复。

依据：[初始化/循环入口](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:408)、[置 true 与使用](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:610)、[循环跳转](/home/dxxdx/debug-ft232./debug-ft232/RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c:899)。这是条件性静态缺陷；纯编程页的固定形态未必触发。CH32 每次 shift 都重新计算 `suppress_gpio`，没有同样的局部标志残留，不能为追求一致而搬回这个问题。

另外，BL702 对长命令的擦除识别只看长度/读写/首字节，不检查后续字节和 TAP/IR；JTAG 的各类 SIO reset/purge 都调用整体 reset。CH32 更严格的擦除条件和 RX/TX 分开 purge 本身不是缺陷。

**已经对齐或没有发现确定错误的部分**

- 基本 MPSSE 字节/位命令集合、LSB/MSB 数据排列、读回按字节入队，以及 `4B 07 7F` 的八拍处理与 BL702 基本相符。
- 常规处理已有全局关中断，并在锁内复查 reset/purge；不是只关 USB 中断。依据：[port 临界区](/home/dxxdx/back-ft232/CH32_FT232/usb_bsp/ft232MpssePort.c:134)、[CSR 保存恢复](/home/dxxdx/back-ft232/CH32_FT232/gpio_toggle/JtagGpioInterrupt.h:13)。USB 头文件里“只屏蔽 USBD”的注释不能替代实际调用链判断。
- RX 满时的整包背压、TX 写前容量检查、队列跨环尾取数，以及 32 位 `remaining` 保存最大 65536 字节长度，未发现明显的普通收包越界。
- 两边保留 FTDI 数据包的 `31 60` 头、VID/PID `0403:6010`、bcdDevice `0500`；CH32 多 CDC 接口属于 USB 组成差异，静态上不能把它列为 Flash 位流错误的根因。
- 当前 CH32 没有上次追加的每字 16 µs/页尾 22 µs 计时路径，BL702 对照源码也没有同款路径；这不能列为“没有模仿 BL702”的遗漏。

建议后续修复顺序：先保证板型与硬件相符；然后处理编程页收齐条件；再分别审查 DR32/擦除实际时序、TX 最大回复需求，以及 DR32 优化的输入兼容和命令完成边界。本轮仅提交此审查记录，以上修复尚未实施。
