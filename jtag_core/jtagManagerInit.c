//
// Created by dxxdx on 2026/9/8.
//

#include "jtagManager.h"

static void jtag_clear_parser(JtagMpsseState *state);
static void jtag_clear_gowin(JtagGowinState *state);

void JTAGManager_Init(const JTAGManager *const self)
{
    JTAGManager_Reset(self);
}

void JTAGManager_Reset(const JTAGManager *const self)
{
    JTAGManager_RxPurge(self);
    JTAGManager_TxPurge(self);
}

void JTAGManager_RxPurge(const JTAGManager *const self)
{
    /* 半条 MPSSE 命令依赖已经丢弃的前缀，必须和输入队列一起作废。 */
    jtag_clear_parser(&self->state->mpsse);
    jtag_clear_gowin(&self->state->gowin);
    self->config->rx->ops->clear(self->config->rx);
}

void JTAGManager_TxPurge(const JTAGManager *const self)
{
    self->config->tx->ops->clear(self->config->tx);
}

static void jtag_clear_parser(JtagMpsseState *const state)
{
    state->remaining = 0U;
    state->phase = JTAG_MPSSE_COMMAND;
    state->opcode = 0U;
    state->argument_count = 0U;
    /* 参数数组随 argument_count 失效，下次接收会覆盖，不做无意义清零。 */
}

static void jtag_clear_gowin(JtagGowinState *const state)
{
    state->transfer_collecting = 0U;
    state->transfer_ready = 0U;
    state->transfer_overflow = 0U;
    state->long_clock_candidate = 0U;
    state->long_clock_suppress = 0U;
    state->ir_pending = 0U;
    state->ir_low7 = 0U;
    state->program_active = 0U;
    state->program_word_stage = 0U;
    /* program_word 随 stage=0 失效，下一组 DR32 会完整覆盖，不浪费启动时间。 */
}
