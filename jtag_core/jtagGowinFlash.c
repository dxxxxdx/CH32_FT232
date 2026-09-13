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

static uint8_t jtag_gowin_tap_next(uint8_t state, uint8_t tms);
static void jtag_gowin_tap_shift_tms(JtagGowinState *state,
                                     uint8_t data,
                                     uint8_t bits);

uint8_t JtagGowinFlash_IsEraseWaitCandidate(
    const JTAGManager *const self, uint8_t opcode, uint32_t byte_count)
{
    return ((self->state->gowin.current_instruction == JTAG_GOWIN_IR_ERASE) &&
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

uint8_t JtagGowinFlash_CaptureProgramData(const JTAGManager *const self,
                                           uint8_t data,
                                           uint8_t bits)
{
    JtagGowinState *const gowin = &self->state->gowin;
    const JtagMpsseState *const mpsse = &self->state->mpsse;

    if (gowin->program_active == 0U)
    {
        return 0U;
    }

    if (mpsse->opcode == 0x11U)
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
                gowin->program_word[byte_index] = data;
                gowin->program_word_stage++;
                return 1U;
            }
            gowin->program_active = 0U;
            gowin->program_word_stage = 0U;
        }
    }
    else if ((mpsse->opcode == 0x13U) && (bits == 7U))
    {
        if (gowin->program_word_stage == 3U)
        {
            gowin->program_word[3] = data;
            gowin->program_word_stage = 4U;
            return 1U;
        }
        gowin->program_active = 0U;
        gowin->program_word_stage = 0U;
    }
    return 0U;
}

uint8_t JtagGowinFlash_IsProgramDr32Tail(const JTAGManager *const self,
                                          uint8_t bits)
{
    return ((self->state->gowin.program_active != 0U) &&
            (self->state->mpsse.opcode == 0x4BU) &&
            (bits == 1U) &&
            (self->state->gowin.program_word_stage == 4U)) ? 1U : 0U;
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
            gowin->program_active =
                (instruction == JTAG_GOWIN_IR_PROGRAM) ? 1U : 0U;
            gowin->program_word_stage = 0U;
            if (instruction == JTAG_GOWIN_IR_ERASE)
            {
                gowin->erase_wait_clocked = 0U;
            }
        }
        gowin->ir_pending = 0U;
    }

    if ((opcode & MPSSE_WRITE_TMS) != 0U)
    {
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
        data = (uint8_t)(data >> 1U);
    }
}
