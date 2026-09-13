# JTAG/MPSSE 核边界

当前自有代码只分三层；各对象在所属模块静态装配，`main.c` 只负责启动和轮询顺序：

- `usb_bsp/`：CH32 USB 外设、枚举、FT2232 控制请求和端点邮箱。
- `jtag_core/`：MPSSE 解析、Gowin 特例、JTAG 状态以及两个 RB 的 ops 接口。
- `gpio_toggle/`：PB13/PB15/PB14/PA8 的寄存器配置与实际翻转。

## 原始 MPSSE 边界

USB/BSP 层对外只交付裸 MPSSE 数据：

```c
uint16_t Ft232Usbd_MpsseRxPeek(const Ft232Usbd *self,
                               const uint8_t **data);
void Ft232Usbd_MpsseRxConsume(const Ft232Usbd *self);
Ft232UsbdResult Ft232Usbd_MpsseTxWrite(const Ft232Usbd *self,
                                       const uint8_t *data,
                                       uint16_t length);
```

OUT 的 1..64 字节有效载荷原样交给 MPSSE 核。TX 接口接收 1..62 字节裸回复，
USB/BSP 层在自己的 64 字节静态缓冲中补 `31 60`，再复制进 PMA。
空闲时不提交只有 `31 60` 的状态包。

`MpssePortOps` 是第一层发布的边界。JTAG 层只认识裸字节、事件和背压结果，
不包含 `ft232Usbd.h`；USB/BSP 层也不包含任何 JTAG 头文件。
具体 FT232 到 MPSSE port 的映射封装在 `usb_bsp/ft232MpssePort.c`。

## RB 所有权

RX/TX 各自是一个静态 `JtagRingBuffer`：

```text
JTAGManager
  ├─ rx -> JtagRingBuffer -> ops + state[2048]
  └─ tx -> JtagRingBuffer -> ops + state[2048]
```

数组、读写索引和 `used` 只由 `jtagRingBuffer.c` 修改。
Manager 通过 `JtagRingBufferOps` 的 `clear/used/free/front/take/put/write/peek/consume`
访问队列，不再持有 RB 实体，也不跨层访问索引。

## GPIO 边界

根目录 `GPIO_Cfg.h` 根据板级宏生成四个独立的静态引脚对象，
`gpio_toggle/GPIO_Cfg.c` 只保留初始化和寄存器翻转实现。

JTAG 核只调用 `JtagIoOps` 的命令级操作。逐边沿仍直接读写 CH32 的
`OUTDR/INDR`；普通 LSB/MSB/TMS 移位和 Gowin DR32 合并保持原来的
寄存器顺序和延时。Gowin Flash 控制流跟踪 TAP/IR，`0x75` 后只输出一次
约 180ms 的连续擦除窗口，不依赖主机声明的 MPSSE 时钟档位。

## 保持不变的协议行为

- MPSSE 解析状态跨 USB 包保留。
- 一个 64 字节 OUT 包空间不足时整包背压，不做部分复制。
- USB 包持续并入 MPSSE 流，不把 libusb transfer 长度误当成 Gowin 页边界。
- `4B 07 7F` 仍按八个 TMS 时钟执行。
- `0x86` 两个分频参数仍直接丢弃，TCK 使用当前固定档位。
- 未知命令仍回复 `FA opcode`。
- reset、两种 purge 和 MPSSE fault 的映射顺序不变。
- Manager 执行 GPIO 时由 port 临界区屏蔽 USBD IRQ；硬件端点保持 NAK，
  完成不可拆分时序后再继续接收 bulk OUT。
