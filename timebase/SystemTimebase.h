#ifndef CH32_FT232_SYSTEM_TIMEBASE_H
#define CH32_FT232_SYSTEM_TIMEBASE_H

#include <stdint.h>
#include "ch32v20x.h"

typedef struct
{
    SysTick_Type *const registers;
} SystemTimebase;

extern const SystemTimebase SystemTimebase0;

/* 启动期 Delay_Ms/Us 共用 SysTick；必须在最后一次启动延时之后、
 * USB 中断使能之前调用一次。运行期所有使用者只读，不再调用 Delay_*。
 */
void SystemTimebase_Init(const SystemTimebase *self);

static inline __attribute__((always_inline))
uint32_t SystemTimebase_Now(const SystemTimebase *const self)
{
    /* RV32 只读 CNT 低字，避免 64 位读取和高低字撕裂；短时间间隔
     * 用无符号差值计算，允许低 32 位回绕，不需要维护第二份软件计数。
     */
    const volatile uint32_t *const low =
        (const volatile uint32_t *)(uintptr_t)&self->registers->CNT;

    return *low;
}

#endif
