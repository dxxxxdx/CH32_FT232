#ifndef CH32_FT232_HOOK_H
#define CH32_FT232_HOOK_H

#include <stdint.h>

/* 默认实现均为空，板级代码提供同名强符号即可接管。
 * UART RX 在收到的数据离开 DMA 环形区时触发；UART TX 在 DMA 发送完成时触发。
 * JTAG 在一轮 service 首次真正产生 TCK 前触发，不进入逐位翻转热路径。
 * HardFault 位于异常上下文。所有钩子都必须快进快出。
 */
void Hook_UartReceived(uint16_t length);
void Hook_UartSent(uint16_t length);
void Hook_JtagToggleStart(void);
void Hook_HardFault(void);

#endif /* CH32_FT232_HOOK_H */
