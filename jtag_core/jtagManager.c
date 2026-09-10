#include "jtagManager.h"

/* 命令集合与普通 GPIO 时序来源：BL702 firmware/app/usb2uartjtag/jtag_process.c。
 * 这里仅承接已经去掉 USB 层的 MPSSE 字节流，不增加新的命令语义。
 */
#define MPSSE_BIT_MODE (0x02U)
#define MPSSE_LSB_FIRST (0x08U)
#define MPSSE_READ_TDO (0x20U)
#define MPSSE_WRITE_TMS (0x40U)
#define MPSSE_BAD_COMMAND (0xFAU)
#define MPSSE_LONG_CLOCK_MIN_BYTES (8000U)
/* 单次 service 的短期调度状态，长期解析状态全部保留在 self->state->mpsse。 */
typedef struct {
    uint32_t clocks_left;
    uint16_t input_left;
    uint8_t stopped;
    JtagServiceResult result;
} JtagServiceWork;

static void jtag_process_command(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_process_arguments(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_process_shift(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_stop(JtagServiceWork *const work, JtagServiceResult result);
static uint8_t jtag_rx_take(const JTAGManager *const self, JtagServiceWork *const work);
static void jtag_tx_put(const JTAGManager *const self, uint8_t value);
static uint8_t jtag_gowin_long_clock_byte(const JTAGManager *self, uint8_t data);
static uint8_t jtag_gowin_capture_program_data(const JTAGManager *self,
                                                uint8_t data,
                                                uint8_t bits);
static uint8_t jtag_gowin_is_program_dr32_tail(const JTAGManager *self,
                                                uint8_t bits);
static void jtag_gowin_observe_ir(const JTAGManager *self,
                                  uint8_t data,
                                  uint8_t bits);

JTAG_MANAGER_DEFINE(JTAGManager0, JtagIo0);

JtagServiceResult JTAGManager_Service(const JTAGManager *const self, uint32_t clock_budget)
{
    JtagServiceWork work = {
        .clocks_left = (self->state->gowin.transfer_ready != 0U)
                           ? 0xFFFFFFFFUL : clock_budget,
        .input_left = self->config->rx->ops->used(self->config->rx),
        .stopped = 0U,
        .result = JTAG_SERVICE_IDLE
    };

    if (self->state->gowin.transfer_overflow != 0U) {
        return JTAG_SERVICE_RX_TRANSFER_OVERFLOW;
    }
    if (self->state->gowin.transfer_collecting != 0U) {
        return JTAG_SERVICE_WAIT_TRANSFER;
    }
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

    if ((self->state->gowin.transfer_ready != 0U) &&
        (self->config->rx->ops->used(self->config->rx) == 0U)) {
        self->state->gowin.transfer_ready = 0U;
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
            (((self->state->mpsse.opcode & MPSSE_READ_TDO) == 0U) &&
             (self->state->mpsse.remaining >= MPSSE_LONG_CLOCK_MIN_BYTES))
                ? 1U : 0U;
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
    uint8_t reply = 0U;
    uint8_t data;

    /* 所有可暂停条件在取走数据和操作 GPIO 之前检查。 */
    if (((self->state->mpsse.opcode & MPSSE_READ_TDO) != 0U) &&
        (self->config->tx->ops->free(self->config->tx) == 0U)) {
        jtag_stop(work, JTAG_SERVICE_WAIT_TX);
        return;
    }

    data = self->config->rx->ops->front(self->config->rx);
    if ((byte_mode != 0U) && (jtag_gowin_long_clock_byte(self, data) != 0U)) {
        (void)jtag_rx_take(self, work);
        self->state->mpsse.remaining--;
        if (self->state->mpsse.remaining == 0U) {
            self->state->gowin.long_clock_suppress = 0U;
            self->state->mpsse.phase = JTAG_MPSSE_COMMAND;
        }
        return;
    }

    program_dr32_tail = jtag_gowin_is_program_dr32_tail(self, bits);
    if (work->clocks_left < ((program_dr32_tail != 0U) ? 32U : bits)) {
        jtag_stop(work, JTAG_SERVICE_BUDGET_REACHED);
        return;
    }

    data = jtag_rx_take(self, work);
    jtag_gowin_observe_ir(self, data, bits);
    program_dr32_tail = jtag_gowin_is_program_dr32_tail(self, bits);
    suppress_gpio = jtag_gowin_capture_program_data(self, data, bits);

    if (program_dr32_tail != 0U) {
        self->config->io->ops->clock_program_dr32(
            self->config->io, self->state->gowin.program_word, data);
        self->state->gowin.program_word_stage = 0U;
        work->clocks_left -= 32U;
    } else if (suppress_gpio != 0U) {
        /* 24+7+1 位要到最后一条 TMS 命令才一次性产生全部 32 个时钟。 */
    } else if ((self->state->mpsse.opcode & MPSSE_WRITE_TMS) != 0U) {
        reply = self->config->io->ops->shift_tms(self->config->io, data, bits);
    } else if ((self->state->mpsse.opcode & MPSSE_LSB_FIRST) != 0U) {
        reply = self->config->io->ops->shift_lsb(self->config->io, data, bits);
    } else if ((self->state->mpsse.opcode & MPSSE_BIT_MODE) != 0U) {
        /* BL702 的 MSB 位命令只有 0x13/0x17，仅输出，不采样 TDO。 */
        self->config->io->ops->shift_msb_output(self->config->io, data, bits);
    } else {
        reply = self->config->io->ops->shift_msb(self->config->io, data, bits);
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

static uint8_t jtag_gowin_long_clock_byte(const JTAGManager *const self,
                                           uint8_t data)
{
    JtagGowinState *const gowin = &self->state->gowin;

    if (gowin->long_clock_suppress != 0U) {
        /* 首字节已经一次性输出完整擦除时钟，余下零流只维持 MPSSE 对齐。 */
        return 1U;
    }
    if (gowin->long_clock_candidate == 0U) {
        return 0U;
    }

    gowin->long_clock_candidate = 0U;
    if (data != 0U) {
        /* 普通的大块配置数据不能误判成擦除空时钟。 */
        return 0U;
    }

    gowin->long_clock_suppress = 1U;
    self->config->io->ops->clock_erase(self->config->io);
    return 1U;
}

static uint8_t jtag_gowin_capture_program_data(const JTAGManager *const self,
                                                uint8_t data,
                                                uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;
    const JtagMpsseState *const mpsse = &self->state->mpsse;

    if (gowin->program_active == 0U) {
        return 0U;
    }

    if (mpsse->opcode == 0x11U) {
        const uint32_t initial =
            ((uint32_t)mpsse->arguments[0] |
             ((uint32_t)mpsse->arguments[1] << 8U)) + 1U;

        if (initial == 3U) {
            const uint8_t byte_index = (uint8_t)(initial - mpsse->remaining);

            if ((byte_index < 3U) && (gowin->program_word_stage == byte_index)) {
                gowin->program_word[byte_index] = data;
                gowin->program_word_stage++;
                return 1U;
            }
            gowin->program_active = 0U;
            gowin->program_word_stage = 0U;
        }
    } else if ((mpsse->opcode == 0x13U) && (bits == 7U)) {
        if (gowin->program_word_stage == 3U) {
            gowin->program_word[3] = data;
            gowin->program_word_stage = 4U;
            return 1U;
        }
        gowin->program_active = 0U;
        gowin->program_word_stage = 0U;
    }
    return 0U;
}

static uint8_t jtag_gowin_is_program_dr32_tail(const JTAGManager *const self,
                                                uint8_t bits)
{
    return ((self->state->gowin.program_active != 0U) &&
            (self->state->mpsse.opcode == 0x4BU) &&
            (bits == 1U) &&
            (self->state->gowin.program_word_stage == 4U)) ? 1U : 0U;
}

static void jtag_gowin_observe_ir(const JTAGManager *const self,
                                  uint8_t data,
                                  uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;
    const uint8_t opcode = self->state->mpsse.opcode;

    if ((opcode == 0x1BU) && (bits == 7U)) {
        /* Gowin 用 0x1B 给出 IR 低七位，最后一位借下一条 TMS 的 bit7。 */
        gowin->ir_low7 = (uint8_t)(data & 0x7FU);
        gowin->ir_pending = 1U;
    }

    if (((opcode & MPSSE_WRITE_TMS) != 0U) && (gowin->ir_pending != 0U)) {
        if (bits == 1U) {
            const uint8_t instruction =
                (uint8_t)(gowin->ir_low7 | (uint8_t)(data & 0x80U));

            gowin->program_active = (instruction == 0x71U) ? 1U : 0U;
            gowin->program_word_stage = 0U;
        }
        gowin->ir_pending = 0U;
    }
}
