//
// Created by dxxdx on 2026/9/8.
//

#include "jtagManager.h"
#include "jtagGowinFlash.h"

static void jtag_clear_parser(JtagMpsseState *state);

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
    JtagGowinFlash_Reset(self);
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
