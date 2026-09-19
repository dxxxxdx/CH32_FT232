#ifndef CH32_FT232_JTAGMANAGER_H
#define CH32_FT232_JTAGMANAGER_H

#include <stdint.h>
#include "jtagIo.h"
#include "jtagRingBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JTAG_MANAGER_RX_BUFFER_SIZE (4096U)
#define JTAG_MANAGER_TX_BUFFER_SIZE (512U)

/* 当前链接脚本将 .srodata 收进 RAM 的 .data，不能只依靠 const。
 * 显式归入 .rodata.*，由 Link.ld 的 .text 输出段放入 FLASH。
 */
#define JTAG_MANAGER_FLASH __attribute__((section(".rodata.jtag")))

typedef struct {
    const JtagIo *io;
    const JtagRingBuffer *rx;
    const JtagRingBuffer *tx;
} JtagConfig;

typedef enum {
    JTAG_MPSSE_COMMAND = 0,
    JTAG_MPSSE_ARGUMENTS,
    JTAG_MPSSE_SHIFT,
    JTAG_MPSSE_FAULT
} JtagMpssePhase;

/* 跨 USB 包的解析进度只有这一份，包结束不清状态。
 * remaining 在字节命令中按字节计，在位/TMS 命令中按位计。
 * 字节长度字段加一可达 65536，不能使用 uint16_t 保存 remaining。
 * opcode 是命令模式的唯一来源，不再复制成多个标志。
 * GPIO 边沿保持 BL702 普通路径的实际行为：低电平设置输出，高电平采样。
 */
typedef struct {
    uint32_t remaining;
    JtagMpssePhase phase;
    uint8_t opcode;
    uint8_t arguments[2];
    uint8_t argument_count;
} JtagMpsseState;

/* Gowin 特例只由 manager 修改。USB 层只投递连续的 MPSSE 字节流，
 * 不维护页缓存、编程阶段或 DR32 影子状态。
 */
typedef struct {
    uint8_t long_clock_candidate;
    uint8_t long_clock_suppress;
    uint8_t ir_pending;
    uint8_t ir_low7;
    uint8_t tap_state;
    uint8_t current_instruction;
    uint8_t erase_wait_clocked;
    uint8_t erase_wait_armed;
    uint8_t prepare_phase;
    uint8_t program_active;
    uint8_t program_word[4];
    uint8_t program_word_stage;
    /* 0=无；01/81=尚未输出的两拍退出。TAP仍停在真实Exit1，不复制影子状态。 */
    uint8_t program_exit_data;
} JtagGowinState;

typedef struct {
    JtagMpsseState mpsse;
    JtagGowinState gowin;
} JtagState;

_Static_assert(sizeof(JtagState) <= 32U,
               "JTAG state exceeds the CH32V203G6 RAM budget");

/* 描述符和配置放只读区；外接 ops 指向的 GPIO/RB 状态仍然可写。
 * 两个连接指针本身也锁定，Init 和 Service 均不能重新绑定内部对象。
 */
typedef struct {
    const JtagConfig *const config;
    JtagState *const state;
} JTAGManager;

/* RX 4 KiB、TX 512 B 与 manager 分别静态装配。manager 只持有 RB 的 ops 边界，
 * 不再拥有或跨层访问数组和索引；GPIO 同样只通过 JtagIo 的命令级 ops 使用。
 */
#define JTAG_MANAGER_DEFINE(name, io_object)                                 \
    JTAG_RING_BUFFER_DEFINE(name##_rx, JTAG_MANAGER_RX_BUFFER_SIZE);          \
    JTAG_RING_BUFFER_DEFINE(name##_tx, JTAG_MANAGER_TX_BUFFER_SIZE);          \
    static const JtagConfig name##_config JTAG_MANAGER_FLASH = {             \
        .io = &(io_object),                                                   \
        .rx = &name##_rx,                                                     \
        .tx = &name##_tx                                                      \
    };                                                                        \
    static JtagState name##_state;                                            \
    const JTAGManager name JTAG_MANAGER_FLASH = {                            \
        .config = &name##_config,                                             \
        .state = &name##_state                                                \
    }

extern const JTAGManager JTAGManager0;

typedef enum {
    JTAG_SERVICE_IDLE = 0,
    JTAG_SERVICE_WAIT_RX,
    JTAG_SERVICE_WAIT_TX,
    JTAG_SERVICE_BUDGET_REACHED,
    JTAG_SERVICE_INVALID_ARGUMENT
} JtagServiceResult;

typedef enum {
    JTAG_RX_PACKET_ACCEPTED = 0,
    JTAG_RX_PACKET_BACKPRESSURE,
    JTAG_RX_PACKET_INVALID_LENGTH,
    JTAG_RX_PACKET_INVALID_DATA
} JtagRxPacketResult;

/* 对象在编译期完成装配；Init 仅复位解析进度、标志及 RX/TX 索引。
 * 不清空数据数组，不修改固定配置或 GPIO 电平/方向。
 * GPIO 时钟、方向和初始电平由 GPIO 层在首次 Service 前设置。
 * 内部固定 self/config/state/ops 不做防御性判空。
 * 所有接口在同一主循环上下文串行调用，外层只向 Service 交付事件。
 * 再次 Init 前，Service 必须结束对 TX 数据的借用，停止旧事务的交付。
 */
void JTAGManager_Init(const JTAGManager *self);

/* 清空 RX/TX 和解析进度，不主动发送 TAP 复位时钟或改变 GPIO。
 * 调用前 Service 必须结束对 TX 片段的借用，并处理旧传输的取消。
 * 此接口是完整内核复位，不能直接等同于 FTDI 的所有 purge 请求。
 */
void JTAGManager_Reset(const JTAGManager *self);

/* Rx 是主机发给设备的 MPSSE 流，清理时同时丢弃半条命令的解析进度。
 * Tx 是设备回给主机的裸回复。两个接口供 FTDI purge 分别映射，避免把
 * 主机视角的 PURGE_RX/PURGE_TX 和 manager 视角混为一谈。
 */
void JTAGManager_RxPurge(const JTAGManager *self);
void JTAGManager_TxPurge(const JTAGManager *self);

/* 每次接收一个 Full Speed bulk 包并立即并入连续 MPSSE 流。USB transfer
 * 可能被主机合并为任意长度，不能据此推断 Gowin 页边界。单包必须为
 * 1..64 字节，空间不足返回 BACKPRESSURE 且不接收。
 */
JtagRxPacketResult JTAGManager_RxWritePacket(const JTAGManager *self,
                                             const uint8_t *data,
                                             uint16_t length);

/* 只发布队列占用，收包批处理不借用数组或复制解析/TAP状态。 */
uint16_t JTAGManager_RxUsed(const JTAGManager *self);
uint16_t JTAGManager_RxFree(const JTAGManager *self);

/* 推进增量解析和 GPIO 移位，普通路径最多产生 clock_budget 个 TCK 周期。
 * clock_budget 建议至少为 8；不足以执行下一个完整操作时返回预算耗尽。
 * Gowin编程含不可拆的32拍DR及最多10拍退出合并，预算应至少为32。
 * 解析输入的数量另外限制为入口时 RX 的 used。
 * 每个不可拆的位操作或字节移位开始前检查预算及回复容量，不能先产生
 * 时钟再因 TX 满而丢回读数据。只接受 BL702 普通路径已有的 opcode；
 * 纯读命令等未实现命令与 BL702 一样按未知 opcode 回复。
 * 未知 opcode 使用 MPSSE 的 FA+opcode 回复，不能用 USB STALL 代替。
 * 非法位数等无法继续的参数进入 FAULT，返回 INVALID_ARGUMENT，等待复位。
 * 0x80/0x82/0x86 仅消费两个参数；0x81/0x83 回复 01/03；0x87 仅消费。
 * Loopback、分频和其它初始化兼容命令保持 BL702 的占位行为；0x86 的
 * 两字节分频参数始终丢弃，TCK 只由当前 GPIO 移位实现的固定档位决定。
 * Gowin 原子路径不受 USB 分包影响：0x71 编程 IR 后，把 0x11/24 bit、
 * 0x13/7 bit、0x4B/末位合成连续 DR32。
 * 对编程DR后的两拍Update/Idle暂存，后续同TDI、首TMS=0的输出命令收齐后，
 * 合并退出与首批Idle时钟；不增删时钟。等待输入时仍停在Exit1，返回WAIT_RX。
 * 非匹配后继或执行屏障先按原样输出暂存退出；RxPurge/Reset显式取消它。
 * Flash控制流依据 TAP 状态区分 IR 与 DR；只有 0x75 后的大块零等待流才替换成一次
 * 固定连续擦除窗口，不能把普通配置数据或同一等待流的后续分块误判进去。
 * 05/09 完成 SRAM 擦除并经 3A/02 退出后，在真实返回 Idle 时补一次
 * 连续 600 us 准备窗口；普通 SRAM 下载也会增加这一个短窗口。
 * 内核不处理传输 latency，也不等待外层发送完成；Service 每轮优先推进 TX。
 * Manager 自身不改变中断状态；调用者通过数据 port 的临界区
 * 保证一次 Service 内的 GPIO 时序不被新 USB 包打断。
 */
JtagServiceResult JTAGManager_Service(const JTAGManager *self,
                                      uint32_t clock_budget);

/* TxPeek 返回当前连续可读片段的长度，并写入只读指针；跨环尾需再次获取。
 * data 是必须有效的内部输出参数；空队列时长度为零、*data 为空。
 * Service 复制到发送端自有内存后即可 Consume；若发送端直接借用该片段，
 * 必须等传输完成后再 Consume。借用期间 Service 只能追加，不能覆盖。
 * Consume 长度不得超过最近 Peek 的片段长度；由 Service 保证该内部契约。
 * FTDI 状态头、USB 分包和发送完成状态全部留在 usb_bsp 层。
 */
uint16_t JTAGManager_TxPeek(const JTAGManager *self,
                          const uint8_t **data);
void JTAGManager_TxConsume(const JTAGManager *self, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* CH32_FT232_JTAGMANAGER_H */
