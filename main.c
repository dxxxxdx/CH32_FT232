#include "debug.h"
#include "GPIO_Cfg.h"
#include "ft232Usbd.h"
#include "ftdiJtagService.h"
#include "uartForwardService.h"

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    GPIO_Cfg_Init(&GpioCfg0);
    Ft232Usbd_Init(&Ft232Usbd0);
    FtdiJtagService0_Init();
    UartForwardService0_Init();
    Ft232Usbd_InterruptInit(&Ft232Usbd0);
    __NOP();
    while (1)
    {
        FtdiJtagService0_Poll();
        UartForwardService0_Poll();
        GPIO_Cfg_RunLedService(&GpioCfg0);
    }

    return 0;
}
