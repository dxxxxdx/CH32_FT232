#include "uartForwardService.h"

#include "boardtype/BoardConfig.h"
#include "ft232CdcPort.h"
#include "uart2DmaPort.h"

#define UART_FORWARD_SERVICE_FLASH \
    __attribute__((section(".rodata.uart_forward")))

typedef enum
{
    UART_FORWARD_PUMP_IDLE = 0,
    UART_FORWARD_PUMP_PROGRESS,
    UART_FORWARD_PUMP_WAIT,
    UART_FORWARD_PUMP_FAULT
} UartForwardPumpResult;

static UartForwardServiceResult uart_forward_check_faults(
    const UartForwardService *self);
static UartForwardPumpResult uart_forward_pump(
    const ByteStreamPort *source,
    const ByteStreamPort *destination,
    uint16_t transfer_size);
static uint8_t uart_forward_discard_uart_rx(const UartForwardService *self);
static UartForwardServiceResult uart_forward_map_usb_fault(
    ByteStreamPortFault fault);
static UartForwardServiceResult uart_forward_map_uart_fault(
    ByteStreamPortFault fault);

static const UartForwardServiceConfig uart_forward_config
    UART_FORWARD_SERVICE_FLASH = {
    .usb = &UsbCdcPort0,
    .uart = &UartDmaPort0,
    .transfer_size = UART_FORWARD_COPY_CHUNK_SIZE
};

static UartForwardServiceState uart_forward_state;

static const UartForwardService uart_forward_service
    UART_FORWARD_SERVICE_FLASH = {
    .config = &uart_forward_config,
    .state = &uart_forward_state
};

void UartForwardService0_Init(void)
{
    UartForwardService_Init(&uart_forward_service);
}

void UartForwardService0_Poll(void)
{
    uart_forward_service.state->last_result =
        (uint8_t)UartForwardService_Service(&uart_forward_service);
}

UartForwardServiceResult UartForwardService0_LastResult(void)
{
    return (UartForwardServiceResult)uart_forward_service.state->last_result;
}

void UartForwardService_Init(const UartForwardService *const self)
{
    self->state->last_result = UART_FORWARD_SERVICE_IDLE;
    self->config->uart->ops->enable(self->config->uart);
    self->config->usb->ops->enable(self->config->usb);
}

UartForwardServiceResult UartForwardService_Service(
    const UartForwardService *const self)
{
    UartForwardServiceResult result;
    UartForwardPumpResult uart_to_usb;
    UartForwardPumpResult usb_to_uart;

    self->config->uart->ops->service(self->config->uart);
    self->config->usb->ops->service(self->config->usb);

    result = uart_forward_check_faults(self);
    if (result != UART_FORWARD_SERVICE_IDLE)
    {
        return result;
    }
    if (self->config->usb->ops->is_ready(self->config->usb) == 0U)
    {
        return (uart_forward_discard_uart_rx(self) != 0U)
                   ? UART_FORWARD_SERVICE_DISCONNECTED_RX_DISCARDED
                   : UART_FORWARD_SERVICE_WAIT_CONFIGURATION;
    }
    if (self->config->uart->ops->is_ready(self->config->uart) == 0U)
    {
        return uart_forward_check_faults(self);
    }

    /* 先疏通持续由 DMA 产生的 UART RX，再处理主机下发，避免 RX 环形区
     * 因 USB IN 背压而被无声覆盖。
     */
    uart_to_usb = uart_forward_pump(self->config->uart, self->config->usb,
                                    self->config->transfer_size);
    usb_to_uart = uart_forward_pump(self->config->usb, self->config->uart,
                                    self->config->transfer_size);
    self->config->uart->ops->service(self->config->uart);

    if ((uart_to_usb == UART_FORWARD_PUMP_FAULT) ||
        (usb_to_uart == UART_FORWARD_PUMP_FAULT))
    {
        return uart_forward_check_faults(self);
    }
    if ((uart_to_usb == UART_FORWARD_PUMP_PROGRESS) ||
        (usb_to_uart == UART_FORWARD_PUMP_PROGRESS))
    {
        return UART_FORWARD_SERVICE_PROGRESS;
    }
    if ((uart_to_usb == UART_FORWARD_PUMP_WAIT) ||
        (usb_to_uart == UART_FORWARD_PUMP_WAIT))
    {
        return UART_FORWARD_SERVICE_BACKPRESSURE;
    }
    return UART_FORWARD_SERVICE_IDLE;
}

static UartForwardServiceResult uart_forward_check_faults(
    const UartForwardService *const self)
{
    const ByteStreamPortFault usb_fault =
        self->config->usb->ops->fault(self->config->usb);
    const ByteStreamPortFault uart_fault =
        self->config->uart->ops->fault(self->config->uart);

    if (usb_fault != BYTE_STREAM_PORT_FAULT_NONE)
    {
        return uart_forward_map_usb_fault(usb_fault);
    }
    if (uart_fault != BYTE_STREAM_PORT_FAULT_NONE)
    {
        return uart_forward_map_uart_fault(uart_fault);
    }
    return UART_FORWARD_SERVICE_IDLE;
}

static UartForwardPumpResult uart_forward_pump(
    const ByteStreamPort *const source,
    const ByteStreamPort *const destination,
    uint16_t transfer_size)
{
    const uint8_t *data;
    uint16_t length = source->ops->rx_peek(source, &data);
    ByteStreamPortResult result;

    if (length == 0U)
    {
        return UART_FORWARD_PUMP_IDLE;
    }
    if (length > transfer_size)
    {
        length = transfer_size;
    }

    result = destination->ops->tx_write(destination, data, length);
    if (result == BYTE_STREAM_PORT_OK)
    {
        source->ops->rx_consume(source, length);
        return UART_FORWARD_PUMP_PROGRESS;
    }
    if ((result == BYTE_STREAM_PORT_NOT_READY) ||
        (result == BYTE_STREAM_PORT_TX_BUSY))
    {
        return UART_FORWARD_PUMP_WAIT;
    }
    if (result == BYTE_STREAM_PORT_IO_FAULT)
    {
        return UART_FORWARD_PUMP_FAULT;
    }

    /* 数据来自另一个受信端口，长度已受 transfer_size 限制。 */
    __builtin_trap();
}

static uint8_t uart_forward_discard_uart_rx(const UartForwardService *const self)
{
    const uint8_t *data;
    const uint16_t length =
        self->config->uart->ops->rx_peek(self->config->uart, &data);

    (void)data;
    if (length == 0U)
    {
        return 0U;
    }
    self->config->uart->ops->rx_consume(self->config->uart, length);
    return 1U;
}

static UartForwardServiceResult uart_forward_map_usb_fault(
    ByteStreamPortFault fault)
{
    switch (fault)
    {
    case BYTE_STREAM_PORT_FAULT_RX_LENGTH:
        return UART_FORWARD_SERVICE_USB_RX_LENGTH_FAULT;
    case BYTE_STREAM_PORT_FAULT_RX_OVERWRITE:
        return UART_FORWARD_SERVICE_USB_RX_OVERWRITE_FAULT;
    default:
        __builtin_trap();
    }
}

static UartForwardServiceResult uart_forward_map_uart_fault(
    ByteStreamPortFault fault)
{
    switch (fault)
    {
    case BYTE_STREAM_PORT_FAULT_RX_OVERFLOW:
        return UART_FORWARD_SERVICE_UART_RX_OVERFLOW_FAULT;
    case BYTE_STREAM_PORT_FAULT_RX_DMA:
        return UART_FORWARD_SERVICE_UART_RX_DMA_FAULT;
    case BYTE_STREAM_PORT_FAULT_TX_DMA:
        return UART_FORWARD_SERVICE_UART_TX_DMA_FAULT;
    case BYTE_STREAM_PORT_FAULT_UART_PARITY:
        return UART_FORWARD_SERVICE_UART_PARITY_FAULT;
    case BYTE_STREAM_PORT_FAULT_UART_FRAMING:
        return UART_FORWARD_SERVICE_UART_FRAMING_FAULT;
    case BYTE_STREAM_PORT_FAULT_UART_NOISE:
        return UART_FORWARD_SERVICE_UART_NOISE_FAULT;
    case BYTE_STREAM_PORT_FAULT_UART_OVERRUN:
        return UART_FORWARD_SERVICE_UART_OVERRUN_FAULT;
    default:
        __builtin_trap();
    }
}

_Static_assert(UART_FORWARD_COPY_CHUNK_SIZE <= UART_FORWARD_RX_BUFFER_SIZE,
               "copy chunk must fit the UART RX buffer");
_Static_assert(UART_FORWARD_COPY_CHUNK_SIZE <= UART_FORWARD_TX_BUFFER_SIZE,
               "copy chunk must fit the UART TX buffer");
_Static_assert(sizeof(UartForwardServiceState) <= 1U,
               "UART forward service state exceeds its static RAM budget");
