#include "Hook.h"

void Hook_UartReceived(uint16_t length) __attribute__((weak));
void Hook_UartSent(uint16_t length) __attribute__((weak));
void Hook_JtagToggleStart(void) __attribute__((weak));
void Hook_HardFault(void) __attribute__((weak));

void Hook_UartReceived(uint16_t length)
{
    (void)length;
}

void Hook_UartSent(uint16_t length)
{
    (void)length;
}

void Hook_JtagToggleStart(void)
{
}

void Hook_HardFault(void)
{
}
