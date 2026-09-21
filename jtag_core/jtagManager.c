#include "jtagManager.h"

#include "Hook.h"
#include "jtagGowinFlash.h"
#include "jtagTrace.h"
#if JTAG_ACM_TRACE_ENABLED
#include "SystemTimebase.h"
#endif

/* 命令集合与普通 GPIO 时序来源：BL702 firmware/app/usb2uartjtag/jtag_process.c。
 * 这里仅承接已经去掉 USB 层的 MPSSE 字节流，不增加新的命令语义。
 */
#define MPSSE_BIT_MODE (0x02U)
#define MPSSE_LSB_FIRST (0x08U)
#define MPSSE_READ_TDO (0x20U)
#define MPSSE_WRITE_TMS (0x40U)
#define MPSSE_BAD_COMMAND (0xFAU)
/* 单次 service 的短期调度状态，长期解析状态全部保留在 self->state->mpsse。 */
typedef struct {
    uint16_t input_left;
    uint8_t suppress_gpio;
    uint8_t toggle_hook_fired;
} JtagServiceWork;

static void jtag_process_command(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_process_arguments(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_process_shift(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_commit_dr32(const JTAGManager *self, uint8_t exit_data);
static void jtag_shift_consumed(const JTAGManager *const self);
static uint8_t jtag_require_tx(const JTAGManager *self, uint16_t length);
static void jtag_toggle_begin(JtagServiceWork *work);
static uint8_t jtag_rx_take(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_tx_put(const JTAGManager *const self, uint8_t value);

JTAG_MANAGER_DEFINE(JTAGManager0, JtagIo0);

JtagServiceResult JTAGManager_Service(const JTAGManager *const self)
{
    JtagServiceWork work = {
        .input_left = self->config->rx->ops->used(self->config->rx),
        .suppress_gpio = 0U,
        .toggle_hook_fired = 0U
    };

    /* BL702 的 suppress_gpio 在一次 jtag_process 入口清零，goto 继续处理
     * 同一批次时不会清零。这里保留该作用域，避免移植时暗改它的行为。
     */
    while ((work.input_left != 0U) &&
           (self->state->mpsse.phase != JTAG_MPSSE_FAULT) &&
           (self->state->mpsse.phase != JTAG_MPSSE_SEQUENCE_FAULT)) {
        switch (self->state->mpsse.phase) {
        case JTAG_MPSSE_COMMAND:
            jtag_process_command(self, &work);
            break;
        case JTAG_MPSSE_ARGUMENTS:
            jtag_process_arguments(self, &work);
            break;
        case JTAG_MPSSE_SHIFT:
            jtag_process_shift(self, &work);
            break;
        default:
            __builtin_trap();
        }
    }

    if (self->state->mpsse.phase == JTAG_MPSSE_FAULT) {
        return JTAG_SERVICE_TX_OVERFLOW;
    }
    if (self->state->mpsse.phase == JTAG_MPSSE_SEQUENCE_FAULT) {
        return JTAG_SERVICE_SEQUENCE_FAULT;
    }
    /* BL702 每批从 RX[0] 开始；只复位空队列，跨包命令和 DR32 暂存保留。 */
    self->config->rx->ops->clear(self->config->rx);
    return (self->state->mpsse.phase == JTAG_MPSSE_COMMAND)
               ? JTAG_SERVICE_IDLE : JTAG_SERVICE_WAIT_RX;
}

static uint8_t jtag_require_tx(const JTAGManager *const self, uint16_t length)
{
    if (self->config->tx->ops->free(self->config->tx) < length) {
        /* BL702 忽略 Ring_Buffer_Write_Byte 的结果。CH32 保留明确故障，
         * 不覆盖旧回复，也不通过页内解锁/回包把一个执行批次拆开。
         */
        self->state->mpsse.phase = JTAG_MPSSE_FAULT;
        return 0U;
    }
    return 1U;
}

static void jtag_toggle_begin(JtagServiceWork *const work)
{
    if (work->toggle_hook_fired == 0U) {
        work->toggle_hook_fired = 1U;
        Hook_JtagToggleStart();
    }
}

static uint8_t jtag_rx_take(const JTAGManager *const self, JtagServiceWork *const work)
{
    const uint8_t value = self->config->rx->ops->take(self->config->rx);

    work->input_left--;
    return value;
}

static void jtag_tx_put(const JTAGManager *const self, uint8_t value)
{
    self->config->tx->ops->put(self->config->tx, value);
}

static void jtag_process_command(const JTAGManager *const self, JtagServiceWork *const work)
{
    const uint8_t opcode = self->config->rx->ops->front(self->config->rx);

    if (JtagGowinFlash_BeginCommand(self, opcode) == 0U) {
        self->state->mpsse.phase = JTAG_MPSSE_SEQUENCE_FAULT;
        return;
    }
    switch (opcode) {
    case 0x80U:
    case 0x82U:
    case 0x86U:
    case 0x19U:
    case 0x1DU:
    case 0x39U:
    case 0x3DU:
    case 0x11U:
    case 0x15U:
    case 0x31U:
    case 0x35U:
    case 0x6BU:
    case 0x6FU:
    case 0x4BU:
    case 0x4FU:
    case 0x3BU:
    case 0x3FU:
    case 0x1BU:
    case 0x1FU:
    case 0x13U:
    case 0x17U:
        self->state->mpsse.opcode = jtag_rx_take(self, work);
        self->state->mpsse.argument_count = 0U;
        self->state->mpsse.phase = JTAG_MPSSE_ARGUMENTS;
        break;

    case 0x81U:
    case 0x83U:
        if (jtag_require_tx(self, 1U) == 0U) {
            return;
        }
        /* BL702 返回固定状态，不在移植时替换为另一种 GPIO 读取语义。 */
        jtag_tx_put(self, (uint8_t)(jtag_rx_take(self, work) - 0x80U));
        break;

    case 0x84U:
    case 0x85U:
    case 0x87U:
    case 0x8AU:
    case 0x8BU:
    case 0x8CU:
    case 0x8DU:
    case 0x96U:
    case 0x97U:
        /* 与 BL702 一样只消费，不能额外返回字节打乱之后的读回结果。 */
        (void)jtag_rx_take(self, work);
        break;

    default:
        /* 整个错误回复有空间后才消费 opcode，重入时不会重复发送 FA。 */
        if (jtag_require_tx(self, 2U) == 0U) {
            return;
        }
        jtag_tx_put(self, MPSSE_BAD_COMMAND);
        jtag_tx_put(self, jtag_rx_take(self, work));
        break;
    }
}

static void jtag_process_arguments(const JTAGManager *const self, JtagServiceWork *const work)
{
    self->state->mpsse.arguments[self->state->mpsse.argument_count] = jtag_rx_take(self, work);
    self->state->mpsse.argument_count++;

    /* 这三条命令固定消费两个参数；0x82 的 bit1 不是移位模式标志。 */
    if ((self->state->mpsse.opcode == 0x80U) || (self->state->mpsse.opcode == 0x82U) ||
        (self->state->mpsse.opcode == 0x86U)) {
        if (self->state->mpsse.argument_count == 2U) {
            self->state->mpsse.phase = JTAG_MPSSE_COMMAND;
        }
        return;
    }

    if ((self->state->mpsse.opcode & MPSSE_BIT_MODE) != 0U) {
        /* BL702 按长度字节加一执行，完整保留 1..256 拍；GPIO 使用
         * uint16_t 位数，避免参数 FF 加一后被截成零。
         */
        self->state->mpsse.remaining = (uint32_t)self->state->mpsse.arguments[0] + 1U;
        self->state->mpsse.phase = JTAG_MPSSE_SHIFT;
    } else if (self->state->mpsse.argument_count == 2U) {
        self->state->mpsse.remaining =
            ((uint32_t)self->state->mpsse.arguments[0] |
             ((uint32_t)self->state->mpsse.arguments[1] << 8U)) + 1U;
        self->state->gowin.long_clock_candidate =
            JtagGowinFlash_IsEraseWaitCandidate(
                self->state->mpsse.opcode,
                self->state->mpsse.remaining);
        self->state->gowin.long_clock_suppress = 0U;
        self->state->mpsse.phase = JTAG_MPSSE_SHIFT;
    }
}

static void jtag_process_shift(const JTAGManager *const self, JtagServiceWork *const work)
{
    const uint8_t opcode = self->state->mpsse.opcode;
    const uint8_t byte_mode = ((opcode & MPSSE_BIT_MODE) == 0U) ? 1U : 0U;
    const uint16_t bits = (byte_mode != 0U)
                              ? 8U : (uint16_t)self->state->mpsse.remaining;
    uint8_t reply = 0U;
    uint8_t data;

    if (((opcode & MPSSE_READ_TDO) != 0U) && (jtag_require_tx(self, 1U) == 0U)) {
        return;
    }
    data = jtag_rx_take(self, work);
    const uint32_t prepare_clocks = JtagGowinFlash_PrepareClocks(self, data, bits);
    if (prepare_clocks != 0U) {
        jtag_toggle_begin(work);
        self->config->io->ops->clock_program_idle(self->config->io, prepare_clocks);
    }
    if (JtagGowinFlash_SuppressIdle(self, data, bits) != 0U) {
        jtag_shift_consumed(self);
        return;
    }
    const JtagGowinDr32Action capture =
        JtagGowinFlash_CaptureProgramData(self, data, bits);
    if (capture == JTAG_GOWIN_DR32_INVALID) {
        self->state->mpsse.phase = JTAG_MPSSE_SEQUENCE_FAULT;
        return;
    }
    if (capture == JTAG_GOWIN_DR32_ENTER_CAPTURED ||
        capture == JTAG_GOWIN_DR32_TAIL_CAPTURED) {
        /* 只推进命令流的控制TAP；物理进入、数据和提交等完整后缀。
         * 87及USB包边界可以出现在这里，物理TAP保持原来的Idle。
         */
        JtagGowinFlash_ObserveInstruction(self, data, bits);
        jtag_shift_consumed(self);
        return;
    }
    if (capture == JTAG_GOWIN_DR32_CAPTURED) {
        /* 已暂存的 LSB 也必须跳过普通 GPIO；1B/7 是 DR 数据而不是 IR。
         * 只保留原 MSB 路径的批次抑制行为，不让擦除地址污染后续命令。
         */
        if (((opcode & MPSSE_LSB_FIRST) == 0U) &&
            (self->state->gowin.dr32_instruction == 0x71U)) {
            work->suppress_gpio = 1U;
        }
        jtag_shift_consumed(self);
        return;
    }
    if (byte_mode != 0U) {
        const JtagGowinLongClockAction action = JtagGowinFlash_LongClockByte(self, data);

        if (action != JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH) {
            if (action == JTAG_GOWIN_LONG_CLOCK_ERASE) {
                jtag_toggle_begin(work);
                self->config->io->ops->clock_erase(self->config->io);
            }
            jtag_shift_consumed(self);
            return;
        }
    }

    /* 先推进命令流控制状态；COMMIT已经确认完整的DR32及返回后缀。 */
    JtagGowinFlash_ObserveInstruction(self, data, bits);
    if (capture == JTAG_GOWIN_DR32_COMMIT) {
        jtag_toggle_begin(work);
        jtag_commit_dr32(self, data);
        JtagGowinFlash_ProgramWordDone(self);
    } else if ((opcode & MPSSE_WRITE_TMS) != 0U) {
        jtag_toggle_begin(work);
        reply = self->config->io->ops->shift_tms(self->config->io, data, bits);
        JtagTrace_Shift(&JtagTrace0, JTAG_TRACE_TMS, data, bits, reply);
    } else if ((opcode & MPSSE_LSB_FIRST) != 0U) {
        jtag_toggle_begin(work);
        reply = self->config->io->ops->shift_lsb(self->config->io, data, bits);
        JtagTrace_Shift(&JtagTrace0, JTAG_TRACE_LSB, data, bits, reply);
    } else if (work->suppress_gpio == 0U) {
        jtag_toggle_begin(work);
        if (byte_mode != 0U) {
            reply = self->config->io->ops->shift_msb(self->config->io, data, bits);
            JtagTrace_Shift(&JtagTrace0, JTAG_TRACE_MSB, data, bits, reply);
        } else {
            self->config->io->ops->shift_msb_output(self->config->io, data, bits);
            JtagTrace_Shift(&JtagTrace0, JTAG_TRACE_MSB_OUT, data, bits, 0U);
        }
    }
    if ((opcode & MPSSE_READ_TDO) != 0U) {
        jtag_tx_put(self, reply);
    }
    jtag_shift_consumed(self);
}

static void jtag_commit_dr32(const JTAGManager *const self, uint8_t exit_data)
{
    const JtagIo *const io = self->config->io;
    const uint32_t idle_clocks = JtagGowinFlash_PendingIdleClocks(self);
    const uint8_t tail = self->state->gowin.program_word_tail;
    const uint8_t enter_data = self->state->gowin.program_enter_tms;
    if (enter_data != 0U) {
        const uint8_t enter_reply = io->ops->shift_tms(io, enter_data, 3U);
        JtagTrace_Shift(&JtagTrace0, JTAG_TRACE_TMS, enter_data, 3U, enter_reply);
        (void)enter_reply;
    }
#if JTAG_ACM_TRACE_ENABLED
    const uint32_t started = SystemTimebase_Now(&SystemTimebase0);
#endif
    /* 数据已齐，不再取RX、解析命令或推进状态机。七拍返回里的额外
     * Idle并入本地窗口；只保留两拍Update/Idle，再立即开始等待。
     * 本次交付关闭TRACE，进入、DR32、退出、等待之间没有观察器开销。
     */
    io->ops->clock_program_dr32(io, self->state->gowin.program_word, tail);
#if JTAG_ACM_TRACE_ENABLED
    JtagTrace_Program(&JtagTrace0, self->state->gowin.program_word, tail,
                      SystemTimebase_Now(&SystemTimebase0) - started);
#endif
    const uint8_t reply = io->ops->shift_tms(io, exit_data, 2U);
    JtagTrace_Shift(&JtagTrace0, JTAG_TRACE_TMS, exit_data, 2U, reply);
    (void)reply;
    if (idle_clocks == 0U) {
        io->ops->clock_erase(io);
    } else {
        io->ops->clock_program_idle(io, idle_clocks);
    }
}

static void jtag_shift_consumed(const JTAGManager *const self)
{
    if ((self->state->mpsse.opcode & MPSSE_BIT_MODE) != 0U) {
        self->state->mpsse.remaining = 0U;
    } else {
        self->state->mpsse.remaining--;
    }
    if (self->state->mpsse.remaining == 0U) {
        self->state->gowin.long_clock_suppress = 0U;
        self->state->mpsse.phase = JTAG_MPSSE_COMMAND;
    }
}
