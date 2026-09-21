#ifndef CH32_FT232_JTAGMANAGER_H
#define CH32_FT232_JTAGMANAGER_H

#include <stdint.h>
#include "jtagIo.h"
#include "jtagRingBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 与 BL702 usb2uartjtag 的 JTAG 缓冲容量一致，USB 包长仍由 port 定义。 */
#define JTAG_MANAGER_RX_BUFFER_SIZE (4096U)
#define JTAG_MANAGER_TX_BUFFER_SIZE (1024U)

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
    JTAG_MPSSE_FAULT,
    JTAG_MPSSE_SEQUENCE_FAULT
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
    /* 71=编程，75=尚未输出的擦除地址，0=不捕获；不另存 active 副本。 */
    uint8_t dr32_instruction;
    uint8_t tap_state;
    uint8_t program_word[4];
    uint8_t program_word_stage;
    /* enter_tms的bit0恒为1，零表示没有延后的进入序列，不另设标志。
     * stage=5时尾拍也已暂存；有enter_tms时物理TAP仍在Idle。
     * idle_instruction取71/75/0，表示哪一种本地等待已经完成。
     */
    uint8_t program_enter_tms;
    uint8_t program_word_tail;
    uint8_t idle_instruction;
    /* 09后的准备窗口和71字序号只由Gowin内核维护。 */
    uint8_t prepare_pending;
    uint8_t program_dr_count;
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

/* RX 4 KiB、TX 1 KiB 与 manager 分别静态装配。manager 只持有 RB 的 ops 边界，
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
    JTAG_SERVICE_TX_OVERFLOW,
    JTAG_SERVICE_SEQUENCE_FAULT
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
 * service 按 BL702 兼容策略把通道 A 的三种 reset/purge 均映射到这里。
 */
void JTAGManager_Reset(const JTAGManager *self);

/* Rx 是主机发给设备的 MPSSE 流，清理时同时丢弃半条命令的解析进度。
 * Tx 是设备回给主机的裸回复。两个内部步骤用于组成完整 Reset，
 * 不再由 USB 的 PURGE_RX/PURGE_TX 分别调用。
 */
void JTAGManager_RxPurge(const JTAGManager *self);
void JTAGManager_TxPurge(const JTAGManager *self);

/* 每次接收一个 Full Speed bulk 包。批次选择由 service 按 BL702 页头规则
 * 决定；这里不解析 USB 边界，只保存连续 MPSSE 字节。单包必须为
 * 1..64 字节，空间不足返回 BACKPRESSURE 且不接收。
 */
JtagRxPacketResult JTAGManager_RxWritePacket(const JTAGManager *self,
                                             const uint8_t *data,
                                             uint16_t length);

/* 只发布队列占用，收包批处理不借用数组或复制解析状态。 */
uint16_t JTAGManager_RxUsed(const JTAGManager *self);
uint16_t JTAGManager_RxFree(const JTAGManager *self);

/* 与 BL702 一样一次执行整个已收批次，调用者在外围屏蔽可屏蔽中断。
 * 无时钟预算或页内让出。半条命令的相位、
 * DR32 暂存跨批次保留；未知命令回复 FA+opcode，0x86 分频仍只消费参数。
 * 长度 >=8000 且首数据字节为零的非读字节命令，替换为 600000 拍，
 * 即 BL702 参考拍数的四倍；触发规则与每拍延时沿用参考实现。
 * DR32 在 Shift-DR 合并 71 编程及 75 的首个擦除地址，支持
 * 11/13 的 MSB 与 19/1B 的 LSB 24+7+1；擦除地址输出后关闭捕获。
 * 09后至下一15之前，在Idle选IR入口刷新1200拍准备窗口。已输出DR32
 * 随后的两拍/七拍退出，在同一次GPIO处理内完成两拍退出及本地等待：
 * 75擦除600000拍、71地址32拍、71数据24拍。后续纯Idle请求仅消费，
 * 同时支持OFL的TMS和Gowin的零字节流，不依赖诊断TAP或USB续包。
 * TX 容量不足返回 TX_OVERFLOW；已暂存 DR32 的后缀非法返回
 * SEQUENCE_FAULT。两者均停止执行并等待 reset，不静默丢弃暂存位。
 */
JtagServiceResult JTAGManager_Service(const JTAGManager *self);

/* TxPeek 返回从读位置加 offset 起的连续可读片段；跨环尾可借用第二段。
 * data 是必须有效的内部输出参数；空队列时长度为零、*data 为空。
 * Service 复制到发送端自有内存后即可 Consume；若发送端直接借用该片段，
 * 必须等传输完成后再 Consume。借用期间 Service 只能追加，不能覆盖。
 * Consume 长度不得超过从 offset=0 连续借用的片段总长，由 Service 保证。
 * FTDI 状态头、USB 分包和发送完成状态全部留在 usb_bsp 层。
 */
uint16_t JTAGManager_TxPeek(const JTAGManager *self,
                            uint16_t offset,
                            const uint8_t **data);
void JTAGManager_TxConsume(const JTAGManager *self, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* CH32_FT232_JTAGMANAGER_H */
