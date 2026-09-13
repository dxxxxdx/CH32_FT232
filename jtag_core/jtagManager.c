#include "jtagManager.h"

#include "Hook.h"
#include "jtagGowinFlash.h"

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
    uint32_t clocks_left;
    uint16_t input_left;
    uint8_t stopped;
    uint8_t toggle_hook_fired;
    JtagServiceResult result;
} JtagServiceWork;

static void jtag_process_command(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_process_arguments(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_process_shift(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_stop(JtagServiceWork *const work, JtagServiceResult result);
static void jtag_toggle_begin(JtagServiceWork *work);
static uint8_t jtag_rx_take(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_tx_put(const JTAGManager *const self, uint8_t value);

JTAG_MANAGER_DEFINE(JTAGManager0, JtagIo0);

JtagServiceResult JTAGManager_Service(const JTAGManager *const self, uint32_t clock_budget)
{
    JtagServiceWork work = {
        .clocks_left = clock_budget,
        .input_left = self->config->rx->ops->used(self->config->rx),
        .stopped = 0U,
        .toggle_hook_fired = 0U,
        .result = JTAG_SERVICE_IDLE
    };

    if (self->state->mpsse.phase == JTAG_MPSSE_FAULT) {
        return JTAG_SERVICE_INVALID_ARGUMENT;
    }

    while ((work.input_left != 0U) && (work.stopped == 0U)) {
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
        case JTAG_MPSSE_FAULT:
        default:
            __builtin_trap();
        }
    }

    if (work.stopped != 0U) {
        return work.result;
    }
    return (self->state->mpsse.phase == JTAG_MPSSE_COMMAND)
               ? JTAG_SERVICE_IDLE : JTAG_SERVICE_WAIT_RX;
}

static void jtag_stop(JtagServiceWork *const work, JtagServiceResult result)
{
    work->stopped = 1U;
    work->result = result;
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
        if (self->config->tx->ops->free(self->config->tx) == 0U) {
            jtag_stop(work, JTAG_SERVICE_WAIT_TX);
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
        if (self->config->tx->ops->free(self->config->tx) < 2U) {
            jtag_stop(work, JTAG_SERVICE_WAIT_TX);
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
        /* 普通位移位最多 8 位。FTDI 文档通常把 TMS 限为 7 位，但 Gowin
         * Programmer 用 4B 07 7F 合并“7 个复位时钟 + 1 个 Idle 时钟”；
         * BL702 原实现也按 8 位执行，因此这里接受参数 0..7。
         * 更大的外部长度仍停在错误状态，不能让一字节数据产生任意时钟。
         */
        const uint8_t maximum = 7U;
        if (self->state->mpsse.arguments[0] > maximum) {
            self->state->mpsse.phase = JTAG_MPSSE_FAULT;
            jtag_stop(work, JTAG_SERVICE_INVALID_ARGUMENT);
            return;
        }
        self->state->mpsse.remaining = (uint32_t)self->state->mpsse.arguments[0] + 1U;
        self->state->mpsse.phase = JTAG_MPSSE_SHIFT;
    } else if (self->state->mpsse.argument_count == 2U) {
        self->state->mpsse.remaining =
            ((uint32_t)self->state->mpsse.arguments[0] |
             ((uint32_t)self->state->mpsse.arguments[1] << 8U)) + 1U;
        self->state->gowin.long_clock_candidate =
            JtagGowinFlash_IsEraseWaitCandidate(
                self, self->state->mpsse.opcode,
                self->state->mpsse.remaining);
        self->state->gowin.long_clock_suppress = 0U;
        self->state->mpsse.phase = JTAG_MPSSE_SHIFT;
    }
}

static void jtag_process_shift(const JTAGManager *const self, JtagServiceWork *const work)
{
    const uint8_t bits = ((self->state->mpsse.opcode & MPSSE_BIT_MODE) != 0U)
                             ? (uint8_t)self->state->mpsse.remaining : 8U;
    const uint8_t byte_mode =
        ((self->state->mpsse.opcode & MPSSE_BIT_MODE) == 0U) ? 1U : 0U;
    uint8_t program_dr32_tail;
    uint8_t suppress_gpio;
    JtagGowinLongClockAction long_clock_action;
    uint8_t reply = 0U;
    uint8_t data;

    /* 所有可暂停条件在取走数据和操作 GPIO 之前检查。 */
    if (((self->state->mpsse.opcode & MPSSE_READ_TDO) != 0U) &&
        (self->config->tx->ops->free(self->config->tx) == 0U)) {
        jtag_stop(work, JTAG_SERVICE_WAIT_TX);
        return;
    }

    data = self->config->rx->ops->front(self->config->rx);
    long_clock_action = (byte_mode != 0U)
                            ? JtagGowinFlash_LongClockByte(self, data)
                            : JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH;
    if (long_clock_action != JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH) {
        if (long_clock_action == JTAG_GOWIN_LONG_CLOCK_ERASE) {
            jtag_toggle_begin(work);
            self->config->io->ops->clock_erase(self->config->io);
        }
        (void)jtag_rx_take(self, work);
        self->state->mpsse.remaining--;
        if (self->state->mpsse.remaining == 0U) {
            self->state->gowin.long_clock_suppress = 0U;
            self->state->mpsse.phase = JTAG_MPSSE_COMMAND;
        }
        return;
    }

    program_dr32_tail = JtagGowinFlash_IsProgramDr32Tail(self, bits);
    if (work->clocks_left < ((program_dr32_tail != 0U) ? 32U : bits)) {
        jtag_stop(work, JTAG_SERVICE_BUDGET_REACHED);
        return;
    }

    data = jtag_rx_take(self, work);
    JtagGowinFlash_ObserveInstruction(self, data, bits);
    program_dr32_tail = JtagGowinFlash_IsProgramDr32Tail(self, bits);
    suppress_gpio = JtagGowinFlash_CaptureProgramData(self, data, bits);

    if (program_dr32_tail != 0U) {
        jtag_toggle_begin(work);
        self->config->io->ops->clock_program_dr32(
            self->config->io, self->state->gowin.program_word, data);
        self->state->gowin.program_word_stage = 0U;
        work->clocks_left -= 32U;
    } else if (suppress_gpio != 0U) {
        /* 24+7+1 位要到最后一条 TMS 命令才一次性产生全部 32 个时钟。 */
    } else {
        jtag_toggle_begin(work);
        if ((self->state->mpsse.opcode & MPSSE_WRITE_TMS) != 0U) {
            reply = self->config->io->ops->shift_tms(
                self->config->io, data, bits);
        } else if ((self->state->mpsse.opcode & MPSSE_LSB_FIRST) != 0U) {
            reply = self->config->io->ops->shift_lsb(
                self->config->io, data, bits);
        } else if ((self->state->mpsse.opcode & MPSSE_BIT_MODE) != 0U) {
            /* BL702 的 MSB 位命令只有 0x13/0x17，仅输出，不采样 TDO。 */
            self->config->io->ops->shift_msb_output(
                self->config->io, data, bits);
        } else {
            reply = self->config->io->ops->shift_msb(
                self->config->io, data, bits);
        }
    }

    if ((self->state->mpsse.opcode & MPSSE_READ_TDO) != 0U) {
        jtag_tx_put(self, reply);
    }
    if ((program_dr32_tail == 0U) && (suppress_gpio == 0U)) {
        work->clocks_left -= bits;
    }
    if ((self->state->mpsse.opcode & MPSSE_BIT_MODE) != 0U) {
        self->state->mpsse.remaining = 0U;
    } else {
        self->state->mpsse.remaining--;
    }
    if (self->state->mpsse.remaining == 0U) {
        self->state->mpsse.phase = JTAG_MPSSE_COMMAND;
    }
}
