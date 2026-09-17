# FTDI JTAG USB 枚举接入

`ft232Descriptor.c` 提供 FT2232 双接口所需的常量描述符，`ft232Descriptor.h`
提供长度、端点和 FTDI vendor request 常量。USB 驱动拥有 EP0 状态机和端点寄存器，
描述符模块不访问 USB 外设，也不访问 `JTAGManager`。

## 设备形态

- 保留 BL702 使用的 `VID:PID = 0403:6010` 和 `bcdDevice = 0500`，主机按
  FT2232D 兼容设备处理。
- USB 产品名和只读 EEPROM 产品名均为 `Dual RS232-HS`。D2XX 会将双通道
  公布为 `Dual RS232-HS A/B`，Gowin Programmer 因此能在重新打开时选中 A 通道，
  不再依赖将名称截断为单通道 `RS232-HS` 的主机替换库。
- interface 0 是通道 A/JTAG，Bulk IN/OUT 为 `0x81/0x02`。
- interface 1 是通道 B/AUX，Bulk IN/OUT 为 `0x83/0x04`；板级 UART 引脚未指定，
  当前只发布独立原始数据面，不在 USB 层擅自消费为 UART。
- 四个 Bulk 端点的 `wMaxPacketSize` 均为 64，配置描述符总长度为 55 字节。
- `bcdUSB = 0x0210` 只用于 BOS/MS OS 2.0 发现，CH32 仍按 Full Speed 工作。

## EP0 的描述符回复

处理标准 `GET_DESCRIPTOR` 时，用 `wValue` 高字节选择类型、低字节选择索引。
实际发送长度始终是 `min(wLength, 描述符长度)`。

| 请求 | 返回数组 |
| --- | --- |
| Device，type `01` index `00` | `FtdiUsbDeviceDescriptor` |
| Configuration，type `02` index `00` | `FtdiUsbConfigurationDescriptor` |
| String，type `03` index `00` | `FtdiUsbLanguageDescriptor` |
| String，type `03` index `01` | `FtdiUsbManufacturerDescriptor` |
| String，type `03` index `02` | `FtdiUsbProductDescriptor` |
| String，type `03` index `03` | `FtdiUsbSerialDescriptor` |
| BOS，type `0F` index `00` | `FtdiUsbBosDescriptor` |

没有发布 Device Qualifier；Full Speed-only 实现收到该请求时应 STALL。未知类型或索引也应 STALL。
`SET_ADDRESS` 必须在状态阶段完成后应用地址；`SET_CONFIGURATION(1)` 成功后启用两对
Bulk 端点，至此标准 USB 枚举完成。

Windows 读取 BOS 后会发设备到主机的 vendor request：

```text
bmRequestType = C0
bRequest      = 20
wValue        = 0000
wIndex        = 0007
```

返回 `FtdiUsbMsOs20Descriptor`，长度同样截断到 `wLength`。它把 interface 0 绑定为 WinUSB。

## 枚举完成后的 FTDI 控制请求

打开 MPSSE 通道时，上位机通常还会发送 FTDI vendor request。请求码已经在头文件中定义。
通道 A/B 分别使用 FTDI 的 `wIndex = 1/2`，两者都有独立 latency、bitmode 和端点状态。

当前 `Ft232Usbd` 已应答 reset/purge、set/get latency、set bitmode、set baudrate、set data、
set flow control、event/error char、modem status 和 read pins。`SET_BITMODE` 的 `wValue`
高字节只接受复位模式 `00` 和 MPSSE `02`；无数据 SET 正常完成状态阶段。GET latency
返回 1 字节，poll modem status 返回 `31 60`，因此 Linux `ftdi_sio` 探测时不再因 latency
请求收到 STALL。还实现 BL702 的有界 EEPROM word 读取。通道 A 的 reset/purge 通过事件
计数交给 JTAG 主循环映射，SETUP 包不会进入 Manager RX。

WCH EP0 会对 `wValue/wIndex` 整字执行 `ByteSwap`，因此所有非零字段均按
`USBwValue0/1`、`USBwIndex0/1` 的逻辑字节校验；不能把内部整字直接与线上的 `0001`
比较。FT2232C/D 通道 A/B 的 latency 请求使用逻辑 `wIndex=0001/0002`。

latency 值会保存并可读回。SOF 提供 1 ms 时基：有效回复一产生就提交 IN，
无回复且 latency 到期时提交只含 `31 60` 的状态包。MPSSE 数据流里的
`86 lo hi` 由 Manager 完整消费并丢弃，外部设置的 TCK 分频不会改变当前 GPIO 固定档位。
`SIO_SET_BAUDRATE` 不校验编码后的 `wIndex`：libftdi 会在其中混入除数高位和芯片代际信息，
而当前两个数据面都不使用该除数。参数直接丢弃，并由 EP0 返回零长度 Status-IN ACK。

## 64 字节包与 Manager 队列

Manager 的 RX/TX 当前各为 2048 字节，正好容纳 32 个 64 字节 OUT 包，因此无需缩减。
描述符中的 `wMaxPacketSize` 必须保持 64，它描述物理端点事务上限，与软件环形队列总深度无关。

Bulk OUT 的最多 64 字节有效载荷原样提交给 Manager RX。Bulk IN 每包先放两个 FTDI 状态字节，
所以每个 64 字节 IN 包最多从 Manager TX 取 62 字节。Gowin WINUSB 的 MPSSE 同步读取不会像
libftdi 一样持续过滤空状态包，因此空状态只在 OUT 邮箱已消费、Manager 未产生回复且
latency 到期后提交。有效载荷优先，避免空状态抢在 `FA AA` 前完成：

```text
31 60 [最多 62 字节 Manager 回复]
31 60 [latency 到期的空闲状态]
```

TX 满时先发 IN 包再继续运行 Manager。RX 空间不足时不要重新使能 OUT 接收，保持 NAK，
避免覆盖尚未执行的 MPSSE 字节。USB 层每个通道各有一个 64 字节静态 OUT 邮箱，JTAG
USB/BSP 层另有一个 64 字节 MPSSE IN 拼包缓冲，均不在函数栈上。邮箱未消费时相应 OUT 端点保持 NAK；PMA 已接管 IN 数据后
Manager TX 才允许出队。

序列号格式是 `CH32_FTDI_YYMMDDhhmmss`。每次执行 CMake 构建时都以 UTC
时间生成新的 12 字符后缀，不读写 CMake cache。生成头文件作为
`ft232Descriptor.c` 的显式依赖，序列号改变后会重编描述符并重新链接。
最终数组仍是 UTF-16LE 编译期常量，全部存放在 Flash。
