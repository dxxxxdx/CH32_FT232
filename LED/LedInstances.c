#include "LedInstances.h"

/* 运行灯的存在性、引脚和电气属性统一由 boardtype 指定。 */
#if RUN_LED_ENABLED != 0U
LED_DEFINE(RunLed0,
           BOARD_RUN_LED_PORT,
           BOARD_RUN_LED_PORT_CLOCK,
           BOARD_RUN_LED_PIN_NUMBER,
           BOARD_RUN_LED_ACTIVE_LEVEL,
           BOARD_RUN_LED_OUTPUT_TYPE,
           BOARD_RUN_LED_REMAP_REGISTER,
           BOARD_RUN_LED_REMAP_MASK,
           BOARD_RUN_LED_REMAP_VALUE,
           RUN_LED_SERVICE_PERIOD_TICKS);
#endif

void LedInstances_Init(void)
{
#if RUN_LED_ENABLED != 0U
    Led_Init(&RunLed0);
#endif
}

void LedInstances_Service(void)
{
#if RUN_LED_ENABLED != 0U
    Led_Service(&RunLed0);
#endif
}
