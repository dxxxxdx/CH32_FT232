#ifndef CH32_FT232_UART_FORWARD_SERVICE_H
#define CH32_FT232_UART_FORWARD_SERVICE_H

#include <stdint.h>

#include "UartForwardConfig.h"
#include "byteStreamPort.h"

typedef enum
{
    UART_FORWARD_SERVICE_IDLE = 0,
    UART_FORWARD_SERVICE_PROGRESS,
    UART_FORWARD_SERVICE_WAIT_CONFIGURATION,
    UART_FORWARD_SERVICE_BACKPRESSURE,
    UART_FORWARD_SERVICE_DISCONNECTED_RX_DISCARDED,
    UART_FORWARD_SERVICE_USB_RX_LENGTH_FAULT,
    UART_FORWARD_SERVICE_USB_RX_OVERWRITE_FAULT,
    UART_FORWARD_SERVICE_UART_RX_OVERFLOW_FAULT,
    UART_FORWARD_SERVICE_UART_RX_DMA_FAULT,
    UART_FORWARD_SERVICE_UART_TX_DMA_FAULT,
    UART_FORWARD_SERVICE_UART_PARITY_FAULT,
    UART_FORWARD_SERVICE_UART_FRAMING_FAULT,
    UART_FORWARD_SERVICE_UART_NOISE_FAULT,
    UART_FORWARD_SERVICE_UART_OVERRUN_FAULT
} UartForwardServiceResult;

typedef struct
{
    const ByteStreamPort *const usb;
    const ByteStreamPort *const uart;
    const uint16_t transfer_size;
} UartForwardServiceConfig;

typedef struct
{
    volatile uint8_t last_result;
} UartForwardServiceState;

typedef struct
{
    const UartForwardServiceConfig *const config;
    UartForwardServiceState *const state;
} UartForwardService;

void UartForwardService_Init(const UartForwardService *self);
UartForwardServiceResult UartForwardService_Service(
    const UartForwardService *self);

#if UART_FORWARD_ENABLED != 0U
void UartForwardService0_Init(void);
void UartForwardService0_Poll(void);
UartForwardServiceResult UartForwardService0_LastResult(void);
#else
/* 关闭转发时 main 仍维持固定服务拓扑；空实现不会引用 CDC、USART 或 DMA。 */
static inline void UartForwardService0_Init(void)
{
}

static inline void UartForwardService0_Poll(void)
{
}

static inline UartForwardServiceResult UartForwardService0_LastResult(void)
{
    return UART_FORWARD_SERVICE_IDLE;
}
#endif

#endif /* CH32_FT232_UART_FORWARD_SERVICE_H */
