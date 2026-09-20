#include "SystemTimebase.h"

/* WCH SysTick CTLR：STE=1、STCLK=HCLK、STRE=1，STIE=0。
 * 64 位 CMP 置最大值，使 CNT 自由运行而不产生周期中断。
 */
#define SYSTEM_TIMEBASE_FREE_RUNNING (0x0000000DUL)

const SystemTimebase SystemTimebase0
    __attribute__((section(".rodata.system_timebase"))) = {
    .registers = SysTick
};

void SystemTimebase_Init(const SystemTimebase *const self)
{
    self->registers->CTLR = 0U;
    self->registers->CNT = 0U;
    self->registers->CMP = UINT64_MAX;
    self->registers->CTLR = SYSTEM_TIMEBASE_FREE_RUNNING;
}
