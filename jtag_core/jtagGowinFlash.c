#include "jtagGowinFlash.h"

#define MPSSE_BIT_MODE  (0x02U)
#define MPSSE_READ_TDO  (0x20U)
#define MPSSE_WRITE_TMS (0x40U)

/* 最低档 0.4 MHz 的 150 ms 等待流约为 7500 字节。只在已经选中
 * EFLASH_ERASE 时识别，因此不再需要用 8000 字节规避普通配置数据。
 */
#define JTAG_GOWIN_ERASE_WAIT_MIN_BYTES (1024U)

typedef enum
{
    JTAG_TAP_TEST_LOGIC_RESET = 0U,
    JTAG_TAP_RUN_TEST_IDLE,
    JTAG_TAP_SELECT_DR_SCAN,
    JTAG_TAP_CAPTURE_DR,
    JTAG_TAP_SHIFT_DR,
    JTAG_TAP_EXIT1_DR,
    JTAG_TAP_PAUSE_DR,
    JTAG_TAP_EXIT2_DR,
    JTAG_TAP_UPDATE_DR,
    JTAG_TAP_SELECT_IR_SCAN,
    JTAG_TAP_CAPTURE_IR,
    JTAG_TAP_SHIFT_IR,
    JTAG_TAP_EXIT1_IR,
    JTAG_TAP_PAUSE_IR,
    JTAG_TAP_EXIT2_IR,
    JTAG_TAP_UPDATE_IR
} JtagTapState;

typedef enum
{
    JTAG_GOWIN_PREPARE_NONE = 0U,
    JTAG_GOWIN_PREPARE_SRAM_ERASING,
    JTAG_GOWIN_PREPARE_SRAM_DONE,
    JTAG_GOWIN_PREPARE_DISABLED,
    JTAG_GOWIN_PREPARE_WAIT_IDLE
} JtagGowinPreparePhase;

static uint8_t jtag_gowin_tap_next(uint8_t state, uint8_t tms);
static void jtag_gowin_tap_shift_tms(JtagGowinState *state,
                                     uint8_t data,
                                     uint8_t bits);
static uint8_t jtag_gowin_reverse_byte(uint8_t data);
static uint8_t jtag_gowin_dr32_capture_enabled(const JtagGowinState *state);
static void jtag_gowin_prepare_instruction(JtagGowinState *state,
                                            uint8_t instruction);

static void jtag_gowin_prepare_instruction(JtagGowinState *const state,
                                            uint8_t instruction)
{
    /* 只有本模块保存准备阶段；不让 USB 包边界或主机时钟档位决定窗口。
     * OFL 在 09 后还有 NOOP，且 05 后可能轮询 STATUS，均保留当前阶段。
     */
    if (instruction == JTAG_GOWIN_IR_SRAM_ERASE)
    {
        state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_SRAM_ERASING;
    }
    else if ((instruction == JTAG_GOWIN_IR_SRAM_DONE) &&
             (state->prepare_phase == (uint8_t)JTAG_GOWIN_PREPARE_SRAM_ERASING))
    {
        state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_SRAM_DONE;
    }
    else if ((instruction == JTAG_GOWIN_IR_CONFIG_DISABLE) &&
             (state->prepare_phase == (uint8_t)JTAG_GOWIN_PREPARE_SRAM_DONE))
    {
        state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_DISABLED;
    }
    else if ((instruction == JTAG_GOWIN_IR_NOOP) &&
             (state->prepare_phase == (uint8_t)JTAG_GOWIN_PREPARE_DISABLED))
    {
        state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_WAIT_IDLE;
    }
    else if ((instruction != JTAG_GOWIN_IR_NOOP) &&
             (instruction != JTAG_GOWIN_IR_STATUS))
    {
        state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_NONE;
    }
}

uint8_t JtagGowinFlash_TakePrepare(const JTAGManager *const self)
{
    JtagGowinState *const state = &self->state->gowin;

    if ((state->prepare_phase != (uint8_t)JTAG_GOWIN_PREPARE_WAIT_IDLE) ||
        (state->current_instruction != JTAG_GOWIN_IR_NOOP) ||
        (state->tap_state != (uint8_t)JTAG_TAP_RUN_TEST_IDLE))
    {
        return 0U;
    }
    state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_NONE;
    return 1U;
}

uint8_t JtagGowinFlash_IsEraseWaitCandidate(
    const JTAGManager *const self, uint8_t opcode, uint32_t byte_count)
{
    return ((self->state->gowin.current_instruction == JTAG_GOWIN_IR_ERASE) &&
            (self->state->gowin.erase_wait_armed != 0U) &&
            (self->state->gowin.tap_state == (uint8_t)JTAG_TAP_RUN_TEST_IDLE) &&
            ((opcode & (MPSSE_BIT_MODE | MPSSE_READ_TDO)) == 0U) &&
            (byte_count >= JTAG_GOWIN_ERASE_WAIT_MIN_BYTES)) ? 1U : 0U;
}

JtagGowinLongClockAction JtagGowinFlash_LongClockByte(
    const JTAGManager *const self, uint8_t data)
{
    JtagGowinState *const gowin = &self->state->gowin;

    if (gowin->long_clock_suppress != 0U)
    {
        return JTAG_GOWIN_LONG_CLOCK_SUPPRESS;
    }
    if (gowin->long_clock_candidate == 0U)
    {
        return JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH;
    }

    gowin->long_clock_candidate = 0U;
    if (data != 0U)
    {
        /* 外部字节流不满足擦除等待约束时，交还普通 MPSSE 路径。 */
        return JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH;
    }

    gowin->long_clock_suppress = 1U;
    if (gowin->erase_wait_clocked != 0U)
    {
        /* FTDI 会把高频长等待拆成多条命令；同一轮擦除只补一次连续窗口。 */
        return JTAG_GOWIN_LONG_CLOCK_SUPPRESS;
    }

    gowin->erase_wait_clocked = 1U;
    return JTAG_GOWIN_LONG_CLOCK_ERASE;
}

JtagGowinLongClockAction JtagGowinFlash_EraseTmsClock(
    const JTAGManager *const self, uint8_t data, uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;

    /* openFPGALoader 的 FT2232 路径用 4B 05 80 产生长等待。
     * 必须先完成 ERASE 的 DR 移位，再在 Idle 中补窗口；0x75 后、
     * DR 之前也有同形的六拍命令，不能在那里提前开始擦除等待。
     */
    if ((gowin->current_instruction != JTAG_GOWIN_IR_ERASE) ||
        (gowin->erase_wait_armed == 0U) ||
        (gowin->tap_state != (uint8_t)JTAG_TAP_RUN_TEST_IDLE) ||
        (self->state->mpsse.opcode != 0x4BU) ||
        (bits != 6U) || (data != 0x80U))
    {
        return JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH;
    }
    if (gowin->erase_wait_clocked != 0U)
    {
        return JTAG_GOWIN_LONG_CLOCK_SUPPRESS;
    }
    gowin->erase_wait_clocked = 1U;
    return JTAG_GOWIN_LONG_CLOCK_ERASE;
}

uint8_t JtagGowinFlash_ProgramCommandValid(
    const JTAGManager *const self, uint8_t opcode)
{
    const uint8_t stage = self->state->gowin.program_word_stage;

    /* 已暂存的位不能因下一条命令不匹配而静默消失。允许 USB 分包，
     * 但事务形态一旦改变，必须让 manager 进入明确的 MPSSE FAULT。
     */
    if (stage == 0U)
    {
        return 1U;
    }
    if (stage == 3U)
    {
        return ((opcode == 0x13U) || (opcode == 0x1BU)) ? 1U : 0U;
    }
    return ((stage == 4U) && (opcode == 0x4BU)) ? 1U : 0U;
}

static uint8_t jtag_gowin_dr32_capture_enabled(const JtagGowinState *const state)
{
    /* 仅把已有严格24+7+1形态扩展到擦除地址，不扩大普通字节/读回路径。
     * 擦除地址复用同一整字缓存；末位由Observe设置armed后不再捕获。
     * program_active仍只属于71，不能将75退出延后而吞掉首个擦除窗口。
     */
    return (state->program_active != 0U ||
            (state->current_instruction == JTAG_GOWIN_IR_ERASE &&
             state->erase_wait_armed == 0U)) ? 1U : 0U;
}

uint8_t JtagGowinFlash_ProgramDataValid(
    const JTAGManager *const self, uint8_t data, uint8_t bits)
{
    const JtagGowinState *const gowin = &self->state->gowin;
    const JtagMpsseState *const mpsse = &self->state->mpsse;
    const uint8_t stage = gowin->program_word_stage;

    if (stage == 0U)
    {
        return 1U;
    }
    if ((jtag_gowin_dr32_capture_enabled(gowin) == 0U) ||
        (gowin->tap_state != (uint8_t)JTAG_TAP_SHIFT_DR))
    {
        return 0U;
    }
    if (stage < 3U)
    {
        return (((mpsse->opcode == 0x11U) || (mpsse->opcode == 0x19U)) &&
                (mpsse->arguments[0] == 2U) && (mpsse->arguments[1] == 0U) &&
                (mpsse->remaining == (uint32_t)(3U - stage))) ? 1U : 0U;
    }
    if (stage == 3U)
    {
        return (((mpsse->opcode == 0x13U) || (mpsse->opcode == 0x1BU)) &&
                (bits == 7U)) ? 1U : 0U;
    }
    return ((stage == 4U) && (mpsse->opcode == 0x4BU) &&
            (bits == 1U) && ((data & 0x01U) != 0U)) ? 1U : 0U;
}

uint8_t JtagGowinFlash_CaptureProgramData(const JTAGManager *const self,
                                           uint8_t data,
                                           uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;
    const JtagMpsseState *const mpsse = &self->state->mpsse;

    if ((jtag_gowin_dr32_capture_enabled(gowin) == 0U) ||
        (gowin->tap_state != (uint8_t)JTAG_TAP_SHIFT_DR))
    {
        return 0U;
    }

    if ((mpsse->opcode == 0x11U) || (mpsse->opcode == 0x19U))
    {
        const uint32_t initial =
            ((uint32_t)mpsse->arguments[0] |
             ((uint32_t)mpsse->arguments[1] << 8U)) + 1U;

        if (initial == 3U)
        {
            const uint8_t byte_index =
                (uint8_t)(initial - mpsse->remaining);

            if ((byte_index < 3U) &&
                (gowin->program_word_stage == byte_index))
            {
                /* 缓存统一为先发送位在 bit7，GPIO 的整字输出保持一个实现。
                 * 0x19 是 LSB-first；0x11 是 IDE 原有的 MSB-first。
                 */
                gowin->program_word[byte_index] =
                    (mpsse->opcode == 0x19U) ? jtag_gowin_reverse_byte(data) : data;
                gowin->program_word_stage++;
                return 1U;
            }
        }
    }
    else if (((mpsse->opcode == 0x13U) || (mpsse->opcode == 0x1BU)) &&
             (bits == 7U))
    {
        if (gowin->program_word_stage == 3U)
        {
            gowin->program_word[3] =
                (mpsse->opcode == 0x1BU) ? jtag_gowin_reverse_byte(data) : data;
            gowin->program_word_stage = 4U;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t jtag_gowin_reverse_byte(uint8_t data)
{
    data = (uint8_t)(((data & 0x55U) << 1U) | ((data >> 1U) & 0x55U));
    data = (uint8_t)(((data & 0x33U) << 2U) | ((data >> 2U) & 0x33U));
    return (uint8_t)((data << 4U) | (data >> 4U));
}

uint8_t JtagGowinFlash_IsProgramDr32Tail(const JTAGManager *const self,
                                          uint8_t bits)
{
    return ((jtag_gowin_dr32_capture_enabled(&self->state->gowin) != 0U) &&
            (self->state->mpsse.opcode == 0x4BU) &&
            (bits == 1U) &&
            (self->state->gowin.program_word_stage == 4U)) ? 1U : 0U;
}

uint8_t JtagGowinFlash_DeferProgramExit(const JTAGManager *const self,
                                        uint8_t data, uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;

    /* 同板对照发现：两拍退出已完成、首Idle命令尚未跨包收齐时可能漏字。
     * 只延后这个完整输出命令，不改已接收的DR数据，也不预先推进软件TAP。
     * 已合并Idle的命令及其它指令保持原路径；地址DR也采用相同退出保护。
     */
    if ((gowin->program_active == 0U) ||
        (gowin->program_word_stage != 0U) ||
        (gowin->tap_state != (uint8_t)JTAG_TAP_EXIT1_DR) ||
        (self->state->mpsse.opcode != 0x4BU) ||
        (bits != 2U) || ((data & 0x03U) != 0x01U))
    {
        return 0U;
    }
    gowin->program_exit_data = (uint8_t)(data & 0x81U);
    return 1U;
}

uint8_t JtagGowinFlash_CanJoinProgramExit(const JTAGManager *const self,
                                          uint8_t data)
{
    const uint8_t pending = self->state->gowin.program_exit_data;

    /* 首TMS=0才产生Idle自循环。不同TDI不能合成一条TMS命令；读回命令
     * 也不合并，避免把原本无回复的退出两拍混入TDO返回值。
     */
    return ((pending != 0U) && (self->state->mpsse.opcode == 0x4BU) &&
            ((data & 0x01U) == 0U) &&
            ((data & 0x80U) == (pending & 0x80U))) ? 1U : 0U;
}

uint8_t JtagGowinFlash_TakeProgramExit(const JTAGManager *const self)
{
    JtagGowinState *const gowin = &self->state->gowin;
    const uint8_t data = gowin->program_exit_data;

    gowin->program_exit_data = 0U;
    jtag_gowin_tap_shift_tms(gowin, data, 2U);
    return data;
}

void JtagGowinFlash_ObserveInstruction(const JTAGManager *const self,
                                        uint8_t data,
                                        uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;
    const uint8_t opcode = self->state->mpsse.opcode;

    if ((opcode == 0x1BU) && (bits == 7U) &&
        (gowin->tap_state == (uint8_t)JTAG_TAP_SHIFT_IR))
    {
        /* Gowin 用 0x1B 给出 IR 低七位，最后一位借下一条 TMS 的 bit7。 */
        gowin->ir_low7 = (uint8_t)(data & 0x7FU);
        gowin->ir_pending = 1U;
    }

    if (((opcode & MPSSE_WRITE_TMS) != 0U) &&
        (gowin->ir_pending != 0U))
    {
        if ((bits == 1U) &&
            (gowin->tap_state == (uint8_t)JTAG_TAP_SHIFT_IR))
        {
            const uint8_t instruction =
                (uint8_t)(gowin->ir_low7 | (uint8_t)(data & 0x80U));

            gowin->current_instruction = instruction;
            jtag_gowin_prepare_instruction(gowin, instruction);
            gowin->program_active =
                (instruction == JTAG_GOWIN_IR_PROGRAM) ? 1U : 0U;
            gowin->program_word_stage = 0U;
            gowin->erase_wait_armed = 0U;
            if (instruction == JTAG_GOWIN_IR_ERASE)
            {
                gowin->erase_wait_clocked = 0U;
            }
        }
        gowin->ir_pending = 0U;
    }

    if ((opcode & MPSSE_WRITE_TMS) != 0U)
    {
        if ((gowin->current_instruction == JTAG_GOWIN_IR_ERASE) &&
            (gowin->tap_state == (uint8_t)JTAG_TAP_SHIFT_DR) &&
            (bits == 1U) && ((data & 0x01U) != 0U))
        {
            gowin->erase_wait_armed = 1U;
        }
        jtag_gowin_tap_shift_tms(gowin, data, bits);
    }
}

static uint8_t jtag_gowin_tap_next(uint8_t state, uint8_t tms)
{
    static const uint8_t next_state[16][2] JTAG_MANAGER_FLASH = {
        {JTAG_TAP_RUN_TEST_IDLE, JTAG_TAP_TEST_LOGIC_RESET},
        {JTAG_TAP_RUN_TEST_IDLE, JTAG_TAP_SELECT_DR_SCAN},
        {JTAG_TAP_CAPTURE_DR, JTAG_TAP_SELECT_IR_SCAN},
        {JTAG_TAP_SHIFT_DR, JTAG_TAP_EXIT1_DR},
        {JTAG_TAP_SHIFT_DR, JTAG_TAP_EXIT1_DR},
        {JTAG_TAP_PAUSE_DR, JTAG_TAP_UPDATE_DR},
        {JTAG_TAP_PAUSE_DR, JTAG_TAP_EXIT2_DR},
        {JTAG_TAP_SHIFT_DR, JTAG_TAP_UPDATE_DR},
        {JTAG_TAP_RUN_TEST_IDLE, JTAG_TAP_SELECT_DR_SCAN},
        {JTAG_TAP_CAPTURE_IR, JTAG_TAP_TEST_LOGIC_RESET},
        {JTAG_TAP_SHIFT_IR, JTAG_TAP_EXIT1_IR},
        {JTAG_TAP_SHIFT_IR, JTAG_TAP_EXIT1_IR},
        {JTAG_TAP_PAUSE_IR, JTAG_TAP_UPDATE_IR},
        {JTAG_TAP_PAUSE_IR, JTAG_TAP_EXIT2_IR},
        {JTAG_TAP_SHIFT_IR, JTAG_TAP_UPDATE_IR},
        {JTAG_TAP_RUN_TEST_IDLE, JTAG_TAP_SELECT_DR_SCAN}
    };

    return next_state[state][(tms != 0U) ? 1U : 0U];
}

static void jtag_gowin_tap_shift_tms(JtagGowinState *const state,
                                      uint8_t data,
                                      uint8_t bits)
{
    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        state->tap_state = jtag_gowin_tap_next(
            state->tap_state, (uint8_t)(data & 0x01U));
        if (state->tap_state == (uint8_t)JTAG_TAP_TEST_LOGIC_RESET)
        {
            /* 一条 TMS 命令可以先复位再回 Idle，必须在经过 Reset 时失效。 */
            state->current_instruction = 0U;
            state->program_active = 0U;
            state->erase_wait_armed = 0U;
            state->erase_wait_clocked = 0U;
            state->prepare_phase = (uint8_t)JTAG_GOWIN_PREPARE_NONE;
            state->ir_pending = 0U;
        }
        data = (uint8_t)(data >> 1U);
    }
}
