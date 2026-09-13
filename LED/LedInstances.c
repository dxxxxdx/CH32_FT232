#include "LedInstances.h"

/* 状态灯的存在性和板级引脚全部收在 LED 文件夹，不反向污染 GPIO_Cfg。 */
#if RUN_LED_ENABLED != 0U
LED_DEFINE(RunLed0,
           RUN_LED_GPIO_PORT,
           RUN_LED_GPIO_PORT_CLOCK,
           RUN_LED_GPIO_PIN_NUMBER,
           RUN_LED_ACTIVE_LEVEL,
           RUN_LED_OUTPUT_TYPE,
           RUN_LED_REMAP_REGISTER,
           RUN_LED_REMAP_MASK,
           RUN_LED_REMAP_VALUE,
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
