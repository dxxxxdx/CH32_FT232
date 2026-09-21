#include "debug.h"
#include "boardtype/BoardGpio.h"
#include "ft232Usbd.h"
#include "ftdiJtagService.h"
#include "LedInstances.h"
#include "uartForwardService.h"
#include "SystemTimebase.h"
#include "jtagTrace.h"

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    BoardGpio_Init(&BoardGpio0);
    LedInstances_Init();
    Ft232Usbd_Init(&Ft232Usbd0);
    FtdiJtagService0_Init();
    UartForwardService0_Init();
    /* 启动延时结束后统一启动时基，USB批处理不能依赖运行灯是否启用。 */
    SystemTimebase_Init(&SystemTimebase0);
    JtagTrace_Init(&JtagTrace0);
    Ft232Usbd_InterruptInit(&Ft232Usbd0);

    while (1)
    {
        FtdiJtagService0_Poll();
        UartForwardService0_Poll();
        JtagTrace_Poll(&JtagTrace0);
        LedInstances_Service();
    }

    return 0;
}
