# JTAG / BL702 行为移植

本版以本地 `RV-Debugger-BL702` 的提交 `1b27d3f74dfc7cfb157e6560ca8a3e7d964b391a` 为基准，对照实际被 CMake 编译的 `firmware/app/usb2uartjtag/jtag_process.c`，以及 `main.c` 的 `usb_dc_ftdi_send_from_ringbuffer`、`usbd_ftdi.c` 的控制请求处理。没有使用 `jtag_process.c.gowin`。

参考 BL702 源码中默认启用 `MPSSE_GOWIN_PROGRAM_TRANSFER_BATCH=1`、`MPSSE_PROCESS_PACKET_CONTIGUOUS=1`、`MPSSE_GOWIN_PROGRAM_DR32_COALESCE=1`。这些名称描述参考实现，不是本项目可选的 CMake 开关。本项目普通移位走 GPIO，不启用硬件 SPI；板型由 [userconfig.cmake 配置流程](../boardtype/README.md)选择。

## 状态和数据归属

- `ftdiJtagService`：唯一的批次相位 IDLE/COLLECTING/READY/FAULT/SEQUENCE_FAULT；负责 USB 邮箱与内核交接，不拥有页数据副本。
- `jtagManager` / `jtagGowinFlash`：跨批次 MPSSE 相位、长时钟标志、IR pending、控制用 TAP、DR32 用途及唯一 4 字节暂存、本地等待完成类型、09后的准备标志及71的字序号。末拍和三拍进入序列各保存1字节，以原阶段字段区分待提交；JtagState仍为28字节。`dr32_instruction` 替代原 `program_active`，取 71/75/0；擦除地址输出后清零。控制 TAP 仅由 Gowin 内核修改，诊断 TAP 仅作观察，不参与 GPIO 决策。
- `jtagRingBuffer`：唯一 RX 4096 B、TX 1024 B 数组及索引；RX 批次执行完清索引，从 0 开始下一批，TX 保持环形。没有动态内存。
- `ft232Usbd`：64 B OUT 邮箱、64 B IN 拼包区、USB PMA 和端点状态，以及最后一次真实 IN 提交的计时。真实回包由环形队列借用一至两段直接复制进现有拼包区，不再增加 62 B 中间缓存。
- `boardtype/BoardGpio`：CH32 GPIO 执行；硬件映射只读取 `BoardConfig.h` 选择的板型头。22PINOUT 为 TCK=PA6、TMS=PA7、TDI=PA5、TDO=PA4。

## 收包和回包

| 情况 | 当前行为，与 BL702 对应 |
| --- | --- |
| 普通 OUT | 一包即一批，不等待短包或 2 ms；执行完再释放最后一个 USB 邮箱 |
| 页头检测 | 仅扫描新批次首包前 32 字节，查找 `4B 03 03 1B 06 71`，不扩大模式 |
| 编程页收集 | 64 B 包继续收集；非零短包使批次 READY；无超时 |
| 满 4096 B | 等下一次 OUT，再执行满批次；触发它的下一包保留给新批次，不覆盖、不丢弃 |
| OUT ZLP | 普通收集时忽略，不作为页尾；满 4096 B 后的下一次 ZLP 同样触发容量分支 |
| 批次执行 | 全批次屏蔽可屏蔽中断，无 clock_budget，也不因 TX 背压解锁拆批 |
| IN 门控 | 收集/执行时不提交新 IN；之前已提交的包保持原生命周期 |
| 真实 IN | `31 60` 加最多 62 B；跨环尾也尽量凑足 62 B |
| 空状态 IN | 无 RX 事务且真实 TX 为空，距最后真实 IN 提交超过 1 ms 后允许发 `31 60`；空包不刷新计时，后续可继续发 |
| 控制请求 | JTAG 三种 SIO reset/purge 均清整个内核；SET_BITMODE 只接受设置，不隐式复位内核 |

`rx_peek` 以 data=NULL 表示没有 USB 包；data 非空、length=0 表示真实 ZLP。这样才能保留 BL702 在读取新包前检查满批次容量的顺序。

CH32 USB 中断只发布事件，主循环才复位解析器。因此 reset/configuration 后先把 OUT 设为 NAK，主循环清完旧状态再重新接收，避免误消费复位后的首包。GPIO 临界区保存并恢复 CH32 全局中断状态，不无条件打开中断。

## JTAG 行为

| 路径 | 当前实现 |
| --- | --- |
| 普通 LSB/MSB | 按 BL702 先拉低 TCK、写数据、拉高 TCK、采样，结束拉低 TCK；普通路径不额外插 NOP |
| TMS | TDI 取 data bit7，TMS 逐位从低位发；保留参考代码每拍两次 TCK_LOW 访问 |
| 位长度 | 保留 BL702 长度字节加一的 1..256 拍行为，用 uint16_t 承载 |
| 初始化命令 | `80/82/86` 消费两参数；`81/83` 回 `01/03`；`84/85/87/8A/8B/8C/8D/96/97` 只消费；未知命令回 `FA + opcode` |
| IR 判断 | 仅在 Shift-IR 识别指令，`1B/7` 加单拍 TMS 拼成 IR；71 打开编程捕获，75 打开一次擦除地址捕获，DR 数据不会被误认成 IR |
| DR32 | 仅在 Shift-DR 捕获 71 编程及 75 首个地址，支持 `11/13` MSB 与 `19/1B` LSB 的 24+7+1；LSB 字节反转后复用同一 GPIO 函数 |
| 形态不匹配 | 未捕获时其它长度仍走普通路径；开始暂存后严格要求7+1尾及两拍/七拍返回Idle，允许无边沿的87，非法后缀报 SEQUENCE_FAULT 并等待 reset，不丢位继续 |
| DR32提交 | 先收齐24+7+1及返回Idle命令，匹配的三拍进入Shift-DR也暂存，物理TAP在此之前保持Idle；随后连续调用三拍进入、DR32、两拍Update/Idle、定量等待，中间不再解析命令。每个DR32半周期保留volatile两次循环及NOP |
| 编程 Idle | 71/DR32返回后缀收齐后，连续输出DR32及两拍返回，再立即执行本地等待，首个DR32地址后32拍，其余数据字后24拍。后续纯Idle的4B，或1..64字节且首字节为零的19请求只消费 |
| 长时钟 | 非读字节命令长度 >=8000 且首数据为 0，立即连续输出 600000 拍（BL702 参考为 150000 拍），余下数据只消费；每条命令独立识别 |
| 擦除和退出 | 75的首个DR32地址连同两拍返回和600000拍等待连续执行，不再依赖下一条等待指令编码；后续纯Idle的4B或首字节为零的19只消费 |
| 擦前准备 | 观察到IR09之后、下一次15/75/17/05或TAP reset之前，在Idle通过4B四拍进入Shift-IR时先补1200拍准备时钟；不窥探下一条IR的USB数据 |

## 有意保留的参考行为与边界

- 擦除拍数600000，为BL702参考的四倍。当前同一连续GPIO循环在软件时基记录中约337.5ms，约1.778MHz；不是外部仪器测量。准备1200拍、地址32拍、每字24拍来自本板成功直驱诊断；单独增大等待不保证满足9C编程要求。
- 编程批次停在 64 B 整数倍且小于 4096 B 时，即使收到 ZLP，也继续等非零短包；恰好 4096 B 时等下一次 OUT。没有补超时，因此主机不再发包时会一直等。此处按 BL702 移植。
- BL702 `suppress_gpio` 在 `jtag_process()` 入口清零，批次内 `goto` 不清零。原 71/MSB 编程路径继续保留这个作用域，同批其它 MSB 命令可能继续被抑制；新增的 75 或 LSB 捕获不设置此标志。所有已捕获字节立即结束本次处理，避免在 LSB 分支重复输出。
- 长零流规则不检查当前 IR，也不逐字节确认余下数据全零，可能把满足条件的普通数据流当长时钟。这同样按参考代码保留。
- BL702 不检查 TX ring 写入返回值。CH32 在 TX 容量不足时保留明确 `TX_OVERFLOW` 故障，停止继续执行并等 reset；不会静默覆盖/丢回复，也不会页内解锁分批。这是错误边界上的明确差异。
- USB FIFO 与 CH32 PMA、CPU/总线频率、编译选项及函数分层不同。分支、边沿顺序和延时循环按参考移植，擦除拍数有上述四倍差异；不能据此声称实际 TCK 频率或纳秒间隔相同。

## 验证状态

2026-09-21，DR_TRANSACTION 固件 `CH32_FTDI_260921125322` 在 22PINOUT 接线、UART 和 ACM 调试关闭的配置下，通过单页 64 字实际读回、openFPGALoader 完整 Flash 烧录（CRC Success）及独立重新加载检查。用户确认完全断电重上电后 LED 仍闪烁，见[成功记录](../docs/gw1n9-flash-success-20260921.md)。

该固件之前的 ATOMIC_COMMIT 使用相同缓冲、等待拍数和 GPIO 循环，单页仍失败；DR_TRANSACTION 进一步暂存三拍进入序列后通过。两轮擦除前缀都在单个 USB 包内，因此不能将现象简单归结为 USB 丢包或跨包间隔。

匹配的 DR32 事务内已连续执行；准备结束到 IR15、字与字之间以及未匹配的命令形式仍可能有解析或 USB 间隙。没有仪器测得 FPGA 允许的具体最长间隙，不声称整个烧录过程等价于本地直驱。

后续板级配置重构及 GUI 构建变体尚无新的完整板测记录，开启 TRACE 也需单独验证时序。最终修复固件的 Gowin GUI、Windows 和外置 Flash 路径尚未重测。早期失败与直驱实验按阶段保留在[排查归档](../docs/README.md)，不作为当前操作步骤。
