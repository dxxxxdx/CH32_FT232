#include "jtagGowinFlash.h"

#define MPSSE_READ_TDO (0x20U)
#define MPSSE_WRITE_TMS (0x40U)
#define MPSSE_LSB_FIRST (0x08U)
#define GOWIN_IR_PROGRAM (0x71U)
#define GOWIN_IR_ERASE (0x75U)
#define MPSSE_LONG_CLOCK_MIN_BYTES (8000U)
#define MPSSE_PROGRAM_IDLE_MAX_BYTES (64U)
/* 复用已在本板完成64字写读验证的直驱参数。GPIO连续循环约1.778MHz：
 * 准备1200拍约675us，地址32拍约18us，每字24拍约13.5us。
 * 这是同一机器码的软件计时参考；不以主机分频参数推算真实TCK。
 */
#define GOWIN_PREPARE_CLOCKS (1200U)
#define GOWIN_ADDRESS_IDLE_CLOCKS (32U)
#define GOWIN_WORD_IDLE_CLOCKS (24U)

typedef enum
{
    TAP_RESET = 0U, TAP_IDLE, TAP_SELECT_DR, TAP_CAPTURE_DR,
    TAP_SHIFT_DR, TAP_EXIT1_DR, TAP_PAUSE_DR, TAP_EXIT2_DR,
    TAP_UPDATE_DR, TAP_SELECT_IR, TAP_CAPTURE_IR, TAP_SHIFT_IR,
    TAP_EXIT1_IR, TAP_PAUSE_IR, TAP_EXIT2_IR, TAP_UPDATE_IR
} JtagGowinTap;

static uint8_t jtag_gowin_reverse_byte(uint8_t data);
static void jtag_gowin_shift_tms(JtagGowinState *state, uint8_t data, uint16_t bits);

void JtagGowinFlash_Reset(const JTAGManager *const self)
{
    JtagGowinState *const state = &self->state->gowin;
    state->long_clock_candidate = 0U;
    state->long_clock_suppress = 0U;
    state->ir_pending = 0U;
    state->ir_low7 = 0U;
    state->dr32_instruction = 0U;
    /* 沿用 master 的解析起点；主机须发送 TAP reset 使物理状态同步。 */
    state->tap_state = TAP_RESET;
    state->program_word_stage = 0U;
    state->program_enter_tms = 0U;
    state->idle_instruction = 0U;
    state->prepare_pending = 0U;
    state->program_dr_count = 0U;
    /* word 由 stage=0 失效，不清数组，不主动发送 JTAG 时钟。 */
}

uint8_t JtagGowinFlash_BeginCommand(const JTAGManager *const self, uint8_t opcode)
{
    JtagGowinState *const state = &self->state->gowin;
    if (state->program_enter_tms != 0U && state->program_word_stage == 0U) {
        /* 进入序列已经接收但尚未发边沿，只允许接上已支持的24位头。 */
        return (opcode == 0x11U || opcode == 0x19U || opcode == 0x87U) ? 1U : 0U;
    }
    /* 已缓存的位不能被新指令静默丢弃。87 不产生边沿，允许跨包屏障。 */
    return (state->program_word_stage == 0U || opcode == 0x87U ||
            (state->program_word_stage == 3U && (opcode == 0x13U || opcode == 0x1BU)) ||
            (state->program_word_stage >= 4U && opcode == 0x4BU)) ? 1U : 0U;
}

void JtagGowinFlash_ProgramWordDone(const JTAGManager *const self)
{
    JtagGowinState *const state = &self->state->gowin;
    state->program_word_stage = 0U;
    state->program_enter_tms = 0U;
    state->idle_instruction = state->dr32_instruction;
    if (state->dr32_instruction == GOWIN_IR_PROGRAM) {
        state->program_dr_count++;
    }
    /* 75 只捕获首个地址；不能把其长擦除等待误走每字编程 Idle。 */
    if (state->dr32_instruction == GOWIN_IR_ERASE) {
        state->dr32_instruction = 0U;
    }
}

uint32_t JtagGowinFlash_PrepareClocks(const JTAGManager *const self,
                                     uint8_t data, uint16_t bits)
{
    JtagGowinState *const state = &self->state->gowin;
    /* 09之后、下一个15之前，在选择IR的入口刷新准备窗口。
     * 主机可能插入STATUS/ID查询，或把IR拆包；不能依赖未来字节的窥探。
     * 此处仍是物理Idle，不能等收到IR值、已进入Shift-IR后再补Idle时钟。
     */
    if (state->prepare_pending == 0U || state->tap_state != TAP_IDLE ||
        self->state->mpsse.opcode != 0x4BU || bits != 4U || (data & 0x0FU) != 3U) {
        return 0U;
    }
    return GOWIN_PREPARE_CLOCKS;
}

uint8_t JtagGowinFlash_SuppressIdle(const JTAGManager *const self,
                                   uint8_t data, uint16_t bits)
{
    JtagGowinState *const state = &self->state->gowin;
    const JtagMpsseState *const mpsse = &self->state->mpsse;
    if (state->tap_state != TAP_IDLE || state->idle_instruction == 0U) {
        return 0U;
    }
    if (mpsse->opcode == 0x4BU && bits <= 7U &&
        (data & ((1U << bits) - 1U)) == 0U) {
        /* OFL的4B/6/80已由本地连续窗口覆盖，仅消费主机的Idle请求。 */
        return 1U;
    }
    if (mpsse->opcode == 0x19U && data == 0U &&
        (state->idle_instruction == GOWIN_IR_ERASE ||
         mpsse->remaining <= MPSSE_PROGRAM_IDLE_MAX_BYTES)) {
        state->long_clock_candidate = 0U;
        state->long_clock_suppress = 1U;
        return 1U;
    }
    return 0U;
}

uint32_t JtagGowinFlash_PendingIdleClocks(const JTAGManager *const self)
{
    const JtagGowinState *const state = &self->state->gowin;
    if (state->dr32_instruction == GOWIN_IR_ERASE) {
        return 0U;
    }
    return state->program_dr_count == 0U ? GOWIN_ADDRESS_IDLE_CLOCKS : GOWIN_WORD_IDLE_CLOCKS;
}

uint8_t JtagGowinFlash_IsEraseWaitCandidate(uint8_t opcode, uint32_t byte_count)
{
    return (((opcode & MPSSE_READ_TDO) == 0U) &&
            (byte_count >= MPSSE_LONG_CLOCK_MIN_BYTES)) ? 1U : 0U;
}

JtagGowinLongClockAction JtagGowinFlash_LongClockByte(
    const JTAGManager *const self, uint8_t data)
{
    JtagGowinState *const state = &self->state->gowin;

    if (state->long_clock_suppress != 0U)
    {
        return JTAG_GOWIN_LONG_CLOCK_SUPPRESS;
    }
    if (state->long_clock_candidate == 0U)
    {
        return JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH;
    }
    state->long_clock_candidate = 0U;
    if (data != 0U)
    {
        return JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH;
    }
    /* 与 BL702 一样只看长度及首字节；这一条命令的余下数据只消费。 */
    state->long_clock_suppress = 1U;
    return JTAG_GOWIN_LONG_CLOCK_ERASE;
}

JtagGowinDr32Action JtagGowinFlash_CaptureProgramData(const JTAGManager *const self,
                                        uint8_t data, uint16_t bits)
{
    JtagGowinState *const state = &self->state->gowin;
    const JtagMpsseState *const mpsse = &self->state->mpsse;

    if (state->dr32_instruction != 0U && state->tap_state == TAP_IDLE &&
        mpsse->opcode == 0x4BU && bits == 3U && (data & 7U) == 1U) {
        /* 收齐整个DR事务再开始打拍，不能先进入Shift-DR后在里面等包。
         * 唯一控制TAP跟随已消费的命令；此字段说明物理入口尚待输出。
         */
        state->program_enter_tms = data;
        return JTAG_GOWIN_DR32_ENTER_CAPTURED;
    }
    if (state->program_word_stage == 5U) {
        /* 控制TAP已消费末拍。先收齐返回后缀，再一次输出暂存的入口、
         * DR32、Update/Idle和等待，中间不再解析USB命令。
         */
        return (state->tap_state == TAP_EXIT1_DR && mpsse->opcode == 0x4BU &&
                ((bits == 2U && (data & 3U) == 1U) ||
                 (bits == 7U && (data & 0x7FU) == 1U)))
                   ? JTAG_GOWIN_DR32_COMMIT : JTAG_GOWIN_DR32_INVALID;
    }
    if ((state->dr32_instruction == 0U) || (state->tap_state != TAP_SHIFT_DR))
    {
        return JTAG_GOWIN_DR32_PASSTHROUGH;
    }
    if (state->program_word_stage == 4U) {
        if (mpsse->opcode != 0x4BU || bits != 1U || (data & 1U) == 0U) {
            return JTAG_GOWIN_DR32_INVALID;
        }
        state->program_word_tail = data;
        state->program_word_stage = 5U;
        return JTAG_GOWIN_DR32_TAIL_CAPTURED;
    }
    if (((mpsse->opcode == 0x11U) || (mpsse->opcode == 0x19U)) &&
        (mpsse->arguments[0] == 2U) && (mpsse->arguments[1] == 0U))
    {
        const uint32_t index = 3U - mpsse->remaining;

        if ((index < 3U) && (state->program_word_stage == index))
        {
            /* 缓存统一为先发位在 bit7，复用同一个连续 32 拍 GPIO 函数。 */
            state->program_word[index] = (mpsse->opcode & MPSSE_LSB_FIRST)
                                             ? jtag_gowin_reverse_byte(data) : data;
            state->program_word_stage++;
            return JTAG_GOWIN_DR32_CAPTURED;
        }
        return JTAG_GOWIN_DR32_INVALID;
    }
    else if (((mpsse->opcode == 0x13U) || (mpsse->opcode == 0x1BU)) && (bits == 7U))
    {
        if (state->program_word_stage == 3U)
        {
            state->program_word[3] = (mpsse->opcode & MPSSE_LSB_FIRST)
                                         ? jtag_gowin_reverse_byte(data) : data;
            state->program_word_stage = 4U;
            return JTAG_GOWIN_DR32_CAPTURED;
        }
    }
    return (state->program_word_stage == 0U && state->program_enter_tms == 0U)
               ? JTAG_GOWIN_DR32_PASSTHROUGH : JTAG_GOWIN_DR32_INVALID;
}

void JtagGowinFlash_ObserveInstruction(const JTAGManager *const self,
                                     uint8_t data, uint16_t bits)
{
    JtagGowinState *const state = &self->state->gowin;
    const uint8_t opcode = self->state->mpsse.opcode;

    /* 已覆盖的纯Idle在调用前被消费，其它实际转换结束等待抑制。 */
    state->idle_instruction = 0U;

    /* 恢复 master 的 Shift-IR 限制；LSB 擦除地址中的 1B/7 不能冒充 IR。 */
    if ((opcode == 0x1BU) && (bits == 7U) && (state->tap_state == TAP_SHIFT_IR))
    {
        state->ir_low7 = (uint8_t)(data & 0x7FU);
        state->ir_pending = 1U;
    }
    if (((opcode & MPSSE_WRITE_TMS) != 0U) && (state->ir_pending != 0U))
    {
        if ((bits == 1U) && (state->tap_state == TAP_SHIFT_IR))
        {
            const uint8_t instruction = (uint8_t)(state->ir_low7 | (data & 0x80U));

            state->dr32_instruction =
                (instruction == GOWIN_IR_PROGRAM || instruction == GOWIN_IR_ERASE)
                    ? instruction : 0U;
            state->program_word_stage = 0U;
            state->program_enter_tms = 0U;
            state->program_dr_count = 0U;
            if (instruction == 0x09U) {
                state->prepare_pending = 1U;
            } else if (instruction == GOWIN_IR_ERASE || instruction == 0x15U || instruction == 0x17U ||
                       instruction == 0x05U) {
                state->prepare_pending = 0U;
            }
        }
        state->ir_pending = 0U;
    }
    if ((opcode & MPSSE_WRITE_TMS) != 0U) {
        jtag_gowin_shift_tms(state, data, bits);
    }
}

static uint8_t jtag_gowin_reverse_byte(uint8_t data)
{
    data = (uint8_t)(((data & 0x55U) << 1U) | ((data >> 1U) & 0x55U));
    data = (uint8_t)(((data & 0x33U) << 2U) | ((data >> 2U) & 0x33U));
    return (uint8_t)((data << 4U) | (data >> 4U));
}

static void jtag_gowin_shift_tms(JtagGowinState *const state, uint8_t data, uint16_t bits)
{
    /* TAP 转移表恢复自 master。只有 Gowin 内核写控制状态，日志不参与决策。 */
    static const uint8_t next[16][2] JTAG_MANAGER_FLASH = {
        {TAP_IDLE, TAP_RESET}, {TAP_IDLE, TAP_SELECT_DR},
        {TAP_CAPTURE_DR, TAP_SELECT_IR}, {TAP_SHIFT_DR, TAP_EXIT1_DR},
        {TAP_SHIFT_DR, TAP_EXIT1_DR}, {TAP_PAUSE_DR, TAP_UPDATE_DR},
        {TAP_PAUSE_DR, TAP_EXIT2_DR}, {TAP_SHIFT_DR, TAP_UPDATE_DR},
        {TAP_IDLE, TAP_SELECT_DR}, {TAP_CAPTURE_IR, TAP_RESET},
        {TAP_SHIFT_IR, TAP_EXIT1_IR}, {TAP_SHIFT_IR, TAP_EXIT1_IR},
        {TAP_PAUSE_IR, TAP_UPDATE_IR}, {TAP_PAUSE_IR, TAP_EXIT2_IR},
        {TAP_SHIFT_IR, TAP_UPDATE_IR}, {TAP_IDLE, TAP_SELECT_DR}
    };
    for (uint16_t bit = 0U; bit < bits; bit++) {
        state->tap_state = next[state->tap_state][data & 1U];
        if (state->tap_state == TAP_RESET) {
            state->dr32_instruction = 0U;
            state->program_word_stage = 0U;
            state->program_enter_tms = 0U;
            state->idle_instruction = 0U;
            state->prepare_pending = 0U;
            state->program_dr_count = 0U;
            state->ir_pending = 0U;
        }
        data >>= 1U;
    }
}
