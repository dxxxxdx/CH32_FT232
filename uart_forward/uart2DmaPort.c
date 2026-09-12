#include "uart2DmaPort.h"

#include <stdint.h>
#include <string.h>

#include "Hook.h"
#include "UartForwardConfig.h"
#include "ch32v20x.h"

#define UART_DMA_FLASH __attribute__((section(".rodata.uart_dma")))
#define UART_DMA_BUFFER_ALIGNMENT __attribute__((aligned(4)))
#define UART_DMA_BRR_VALUE \
    ((UART_FORWARD_PERIPHERAL_CLOCK_HZ + (UART_FORWARD_BAUD_RATE / 2UL)) / \
     UART_FORWARD_BAUD_RATE)
#define UART_DMA_RX_BUFFER_MASK (UART_FORWARD_RX_BUFFER_SIZE - 1U)
#define UART_DMA_TX_BUFFER_MASK (UART_FORWARD_TX_BUFFER_SIZE - 1U)
#define UART_DMA_STATUS_ERROR_MASK \
    ((uint16_t)(USART_STATR_PE | USART_STATR_FE | USART_STATR_NE | USART_STATR_ORE))
#define UART_DMA_RX_CONFIG \
    ((uint32_t)(DMA_CFGR1_MINC | DMA_CFGR1_PL_0 | DMA_CFGR1_TCIE | \
                DMA_CFGR1_TEIE | DMA_CFGR1_CIRC))
#define UART_DMA_TX_CONFIG \
    ((uint32_t)(DMA_CFGR1_DIR | DMA_CFGR1_MINC | DMA_CFGR1_PL_0))

#if UART_FORWARD_PORT == UART_FORWARD_PORT_PA23
#define UART_DMA_RX_IRQ_HANDLER DMA1_Channel6_IRQHandler
#elif UART_FORWARD_PORT == UART_FORWARD_PORT_PB67
#define UART_DMA_RX_IRQ_HANDLER DMA1_Channel5_IRQHandler
#endif

typedef struct
{
    USART_TypeDef *const usart;
    DMA_TypeDef *const dma;
    DMA_Channel_TypeDef *const rx_dma;
    DMA_Channel_TypeDef *const tx_dma;
    volatile uint32_t *const peripheral_clock_enable;
    volatile uint32_t *const peripheral_reset;
    const uint32_t peripheral_clock_mask;
    const uint32_t peripheral_reset_mask;
    const IRQn_Type rx_irq;
    const uint32_t rx_dma_global_flag;
    const uint32_t rx_dma_complete_flag;
    const uint32_t rx_dma_error_flag;
    const uint32_t tx_dma_global_flag;
    const uint32_t tx_dma_complete_flag;
    const uint32_t tx_dma_error_flag;
    const uint16_t brr;
} Uart2DmaConfig;

typedef struct
{
    uint8_t rx_buffer[UART_FORWARD_RX_BUFFER_SIZE];
    uint8_t tx_buffer[UART_FORWARD_TX_BUFFER_SIZE];
    volatile uint32_t rx_wrap_count;
    uint32_t rx_consumed;
    uint32_t tx_produced;
    uint32_t tx_consumed;
    uint16_t tx_dma_length;
    volatile uint8_t rx_dma_fault;
    uint8_t rx_overflow_fault;
    uint8_t uart_fault;
    uint8_t tx_dma_fault;
    uint8_t tx_dma_active;
    uint8_t initialized;
} Uart2DmaState;

typedef struct
{
    const Uart2DmaConfig *const config;
    Uart2DmaState *const state;
} Uart2Dma;

static void uart2_dma_enable(const ByteStreamPort *self);
static void uart2_dma_service(const ByteStreamPort *self);
static uint8_t uart2_dma_is_ready(const ByteStreamPort *self);
static ByteStreamPortFault uart2_dma_fault(const ByteStreamPort *self);
static uint16_t uart2_dma_rx_peek(const ByteStreamPort *self,
                                  const uint8_t **data);
static void uart2_dma_rx_consume(const ByteStreamPort *self, uint16_t length);
static ByteStreamPortResult uart2_dma_tx_write(const ByteStreamPort *self,
                                               const uint8_t *data,
                                               uint16_t length);
static void uart2_dma_configure(const Uart2Dma *self);
static void uart2_dma_service_uart_errors(const Uart2Dma *self);
static void uart2_dma_service_tx(const Uart2Dma *self);
static void uart2_dma_start_tx(const Uart2Dma *self);
static uint32_t uart2_dma_rx_produced(const Uart2Dma *self);

#if UART_FORWARD_PORT == UART_FORWARD_PORT_PA23
static const Uart2DmaConfig uart2_dma_config UART_DMA_FLASH = {
    .usart = USART2,
    .dma = DMA1,
    .rx_dma = DMA1_Channel6,
    .tx_dma = DMA1_Channel7,
    .peripheral_clock_enable = &RCC->APB1PCENR,
    .peripheral_reset = &RCC->APB1PRSTR,
    .peripheral_clock_mask = RCC_USART2EN,
    .peripheral_reset_mask = RCC_USART2RST,
    .rx_irq = DMA1_Channel6_IRQn,
    .rx_dma_global_flag = DMA_CGIF6,
    .rx_dma_complete_flag = DMA_TCIF6,
    .rx_dma_error_flag = DMA_TEIF6,
    .tx_dma_global_flag = DMA_CGIF7,
    .tx_dma_complete_flag = DMA_TCIF7,
    .tx_dma_error_flag = DMA_TEIF7,
    .brr = (uint16_t)UART_DMA_BRR_VALUE
};
#elif UART_FORWARD_PORT == UART_FORWARD_PORT_PB67
static const Uart2DmaConfig uart2_dma_config UART_DMA_FLASH = {
    .usart = USART1,
    .dma = DMA1,
    .rx_dma = DMA1_Channel5,
    .tx_dma = DMA1_Channel4,
    .peripheral_clock_enable = &RCC->APB2PCENR,
    .peripheral_reset = &RCC->APB2PRSTR,
    .peripheral_clock_mask = RCC_USART1EN,
    .peripheral_reset_mask = RCC_USART1RST,
    .rx_irq = DMA1_Channel5_IRQn,
    .rx_dma_global_flag = DMA_CGIF5,
    .rx_dma_complete_flag = DMA_TCIF5,
    .rx_dma_error_flag = DMA_TEIF5,
    .tx_dma_global_flag = DMA_CGIF4,
    .tx_dma_complete_flag = DMA_TCIF4,
    .tx_dma_error_flag = DMA_TEIF4,
    .brr = (uint16_t)UART_DMA_BRR_VALUE
};
#endif

static Uart2DmaState uart2_dma_state UART_DMA_BUFFER_ALIGNMENT;

static const Uart2Dma uart2_dma UART_DMA_FLASH = {
    .config = &uart2_dma_config,
    .state = &uart2_dma_state
};

static const ByteStreamPortOps uart2_dma_ops UART_DMA_FLASH = {
    .enable = uart2_dma_enable,
    .service = uart2_dma_service,
    .is_ready = uart2_dma_is_ready,
    .fault = uart2_dma_fault,
    .rx_peek = uart2_dma_rx_peek,
    .rx_consume = uart2_dma_rx_consume,
    .tx_write = uart2_dma_tx_write
};

const ByteStreamPort UartDmaPort0 UART_DMA_FLASH = {
    .ops = &uart2_dma_ops,
    .context = &uart2_dma
};

static const Uart2Dma *uart2_dma_from_port(const ByteStreamPort *const self)
{
    return (const Uart2Dma *)self->context;
}

static void uart2_dma_enable(const ByteStreamPort *const self)
{
    const Uart2Dma *const uart = uart2_dma_from_port(self);
    Uart2DmaState *const state = uart->state;

    state->rx_wrap_count = 0U;
    state->rx_consumed = 0U;
    state->tx_produced = 0U;
    state->tx_consumed = 0U;
    state->tx_dma_length = 0U;
    state->rx_dma_fault = 0U;
    state->rx_overflow_fault = 0U;
    state->uart_fault = BYTE_STREAM_PORT_FAULT_NONE;
    state->tx_dma_fault = 0U;
    state->tx_dma_active = 0U;
    state->initialized = 0U;

    uart2_dma_configure(uart);
    __asm volatile ("" ::: "memory");
    state->initialized = 1U;
}

static void uart2_dma_service(const ByteStreamPort *const self)
{
    const Uart2Dma *const uart = uart2_dma_from_port(self);

    uart2_dma_service_uart_errors(uart);
    uart2_dma_service_tx(uart);
}

static uint8_t uart2_dma_is_ready(const ByteStreamPort *const self)
{
    const Uart2DmaState *const state = uart2_dma_from_port(self)->state;

    return ((state->initialized != 0U) && (state->rx_dma_fault == 0U) &&
            (state->rx_overflow_fault == 0U) &&
            (state->uart_fault == BYTE_STREAM_PORT_FAULT_NONE) &&
            (state->tx_dma_fault == 0U)) ? 1U : 0U;
}

static ByteStreamPortFault uart2_dma_fault(const ByteStreamPort *const self)
{
    const Uart2DmaState *const state = uart2_dma_from_port(self)->state;

    if (state->rx_dma_fault != 0U)
    {
        return BYTE_STREAM_PORT_FAULT_RX_DMA;
    }
    if (state->rx_overflow_fault != 0U)
    {
        return BYTE_STREAM_PORT_FAULT_RX_OVERFLOW;
    }
    if (state->uart_fault != BYTE_STREAM_PORT_FAULT_NONE)
    {
        return (ByteStreamPortFault)state->uart_fault;
    }
    return (state->tx_dma_fault != 0U) ? BYTE_STREAM_PORT_FAULT_TX_DMA
                                      : BYTE_STREAM_PORT_FAULT_NONE;
}

static uint16_t uart2_dma_rx_peek(const ByteStreamPort *const self,
                                  const uint8_t **const data)
{
    const Uart2Dma *const uart = uart2_dma_from_port(self);
    Uart2DmaState *const state = uart->state;
    const uint32_t produced = uart2_dma_rx_produced(uart);
    const uint32_t available = produced - state->rx_consumed;
    uint16_t contiguous;
    uint16_t read_pos;

    if (available > UART_FORWARD_RX_BUFFER_SIZE)
    {
        /* 循环 DMA 已追过消费端，旧字节不再可信，必须在 UART 边界显式报错。 */
        state->rx_overflow_fault = 1U;
        *data = (const uint8_t *)0;
        return 0U;
    }
    if (available == 0U)
    {
        *data = (const uint8_t *)0;
        return 0U;
    }

    read_pos = (uint16_t)(state->rx_consumed & UART_DMA_RX_BUFFER_MASK);
    contiguous = (uint16_t)(UART_FORWARD_RX_BUFFER_SIZE - read_pos);
    if ((uint32_t)contiguous > available)
    {
        contiguous = (uint16_t)available;
    }
    __asm volatile ("" ::: "memory");
    *data = &state->rx_buffer[read_pos];
    return contiguous;
}

static void uart2_dma_rx_consume(const ByteStreamPort *const self,
                                 uint16_t length)
{
    const Uart2Dma *const uart = uart2_dma_from_port(self);

    /* consumed 只由主循环写，DMA 与中断只推进生产端，不需要扩大临界区。 */
    uart->state->rx_consumed += length;
    Hook_UartReceived(length);
}

static ByteStreamPortResult uart2_dma_tx_write(
    const ByteStreamPort *const self,
    const uint8_t *const data,
    uint16_t length)
{
    const Uart2Dma *const uart = uart2_dma_from_port(self);
    Uart2DmaState *const state = uart->state;
    const uint32_t used = state->tx_produced - state->tx_consumed;
    uint16_t write_pos;
    uint16_t first_length;

    if ((length == 0U) || (length > UART_FORWARD_TX_BUFFER_SIZE))
    {
        return BYTE_STREAM_PORT_INVALID_LENGTH;
    }
    if (data == (const uint8_t *)0)
    {
        __builtin_trap();
    }
    if (state->initialized == 0U)
    {
        return BYTE_STREAM_PORT_NOT_READY;
    }
    if ((state->rx_dma_fault != 0U) ||
        (state->rx_overflow_fault != 0U) ||
        (state->uart_fault != BYTE_STREAM_PORT_FAULT_NONE) ||
        (state->tx_dma_fault != 0U))
    {
        return BYTE_STREAM_PORT_IO_FAULT;
    }
    if (used > UART_FORWARD_TX_BUFFER_SIZE)
    {
        __builtin_trap();
    }
    if ((uint32_t)length > (UART_FORWARD_TX_BUFFER_SIZE - used))
    {
        return BYTE_STREAM_PORT_TX_BUSY;
    }

    write_pos = (uint16_t)(state->tx_produced & UART_DMA_TX_BUFFER_MASK);
    first_length = (uint16_t)(UART_FORWARD_TX_BUFFER_SIZE - write_pos);
    if (first_length > length)
    {
        first_length = length;
    }
    (void)memcpy(&state->tx_buffer[write_pos], data, first_length);
    if (first_length < length)
    {
        (void)memcpy(state->tx_buffer, &data[first_length],
                     (uint16_t)(length - first_length));
    }
    __asm volatile ("" ::: "memory");
    state->tx_produced += length;
    return BYTE_STREAM_PORT_OK;
}

static void uart2_dma_configure(const Uart2Dma *const self)
{
    const Uart2DmaConfig *const config = self->config;

    RCC->AHBPCENR |= RCC_DMA1EN;
    *config->peripheral_clock_enable |= config->peripheral_clock_mask;
    *config->peripheral_reset |= config->peripheral_reset_mask;
    *config->peripheral_reset &= ~config->peripheral_reset_mask;

    config->rx_dma->CFGR &= ~(uint32_t)DMA_CFGR1_EN;
    config->tx_dma->CFGR &= ~(uint32_t)DMA_CFGR1_EN;
    config->dma->INTFCR =
        config->rx_dma_global_flag | config->tx_dma_global_flag;

    config->usart->CTLR1 = 0U;
    config->usart->CTLR2 = 0U;             /* STOP=00：一个停止位。 */
    config->usart->CTLR3 = 0U;             /* 无流控，DMA 请求稍后单独打开。 */
    config->usart->BRR = config->brr;

    config->rx_dma->PADDR = (uint32_t)(uintptr_t)&config->usart->DATAR;
    config->rx_dma->MADDR = (uint32_t)(uintptr_t)self->state->rx_buffer;
    config->rx_dma->CNTR = UART_FORWARD_RX_BUFFER_SIZE;
    config->rx_dma->CFGR = UART_DMA_RX_CONFIG;

    config->tx_dma->PADDR = (uint32_t)(uintptr_t)&config->usart->DATAR;
    config->tx_dma->MADDR = (uint32_t)(uintptr_t)self->state->tx_buffer;
    config->tx_dma->CNTR = 0U;
    config->tx_dma->CFGR = UART_DMA_TX_CONFIG;

    NVIC_ClearPendingIRQ(config->rx_irq);
    NVIC_EnableIRQ(config->rx_irq);
    config->rx_dma->CFGR |= DMA_CFGR1_EN;
    config->usart->CTLR3 = USART_CTLR3_DMAR;
    config->usart->CTLR1 =
        (uint16_t)(USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE);
}

static void uart2_dma_service_uart_errors(const Uart2Dma *const self)
{
    const uint16_t status = self->config->usart->STATR;

    if ((status & UART_DMA_STATUS_ERROR_MASK) == 0U)
    {
        return;
    }
    /* 仅在硬件报告错误时读 DATAR，避免正常路径与 RX DMA 抢数据。 */
    (void)self->config->usart->DATAR;
    if ((status & USART_STATR_ORE) != 0U)
    {
        self->state->uart_fault = BYTE_STREAM_PORT_FAULT_UART_OVERRUN;
    }
    else if ((status & USART_STATR_FE) != 0U)
    {
        self->state->uart_fault = BYTE_STREAM_PORT_FAULT_UART_FRAMING;
    }
    else if ((status & USART_STATR_NE) != 0U)
    {
        self->state->uart_fault = BYTE_STREAM_PORT_FAULT_UART_NOISE;
    }
    else
    {
        self->state->uart_fault = BYTE_STREAM_PORT_FAULT_UART_PARITY;
    }
}

static void uart2_dma_service_tx(const Uart2Dma *const self)
{
    Uart2DmaState *const state = self->state;
    const uint32_t dma_flags = self->config->dma->INTFR;

    if ((dma_flags & self->config->tx_dma_error_flag) != 0U)
    {
        self->config->tx_dma->CFGR &= ~(uint32_t)DMA_CFGR1_EN;
        self->config->usart->CTLR3 &= (uint16_t)~USART_CTLR3_DMAT;
        self->config->dma->INTFCR = self->config->tx_dma_global_flag;
        state->tx_dma_active = 0U;
        state->tx_dma_fault = 1U;
        return;
    }

    if ((state->tx_dma_active != 0U) &&
        ((dma_flags & self->config->tx_dma_complete_flag) != 0U))
    {
        const uint16_t completed_length = state->tx_dma_length;

        self->config->tx_dma->CFGR &= ~(uint32_t)DMA_CFGR1_EN;
        self->config->usart->CTLR3 &= (uint16_t)~USART_CTLR3_DMAT;
        self->config->dma->INTFCR = self->config->tx_dma_global_flag;
        state->tx_consumed += state->tx_dma_length;
        state->tx_dma_length = 0U;
        state->tx_dma_active = 0U;
        Hook_UartSent(completed_length);
    }

    if ((state->tx_dma_active == 0U) &&
        (state->tx_produced != state->tx_consumed) &&
        (state->rx_dma_fault == 0U) &&
        (state->rx_overflow_fault == 0U) &&
        (state->uart_fault == BYTE_STREAM_PORT_FAULT_NONE) &&
        (state->tx_dma_fault == 0U))
    {
        uart2_dma_start_tx(self);
    }
}

static void uart2_dma_start_tx(const Uart2Dma *const self)
{
    Uart2DmaState *const state = self->state;
    const uint16_t read_pos =
        (uint16_t)(state->tx_consumed & UART_DMA_TX_BUFFER_MASK);
    const uint32_t queued = state->tx_produced - state->tx_consumed;
    uint16_t contiguous =
        (uint16_t)(UART_FORWARD_TX_BUFFER_SIZE - read_pos);

    if ((uint32_t)contiguous > queued)
    {
        contiguous = (uint16_t)queued;
    }

    self->config->tx_dma->CFGR &= ~(uint32_t)DMA_CFGR1_EN;
    self->config->dma->INTFCR = self->config->tx_dma_global_flag;
    self->config->tx_dma->MADDR =
        (uint32_t)(uintptr_t)&state->tx_buffer[read_pos];
    self->config->tx_dma->CNTR = contiguous;
    state->tx_dma_length = contiguous;
    state->tx_dma_active = 1U;
    __asm volatile ("" ::: "memory");
    self->config->tx_dma->CFGR |= DMA_CFGR1_EN;
    self->config->usart->CTLR3 |= USART_CTLR3_DMAT;
}

static uint32_t uart2_dma_rx_produced(const Uart2Dma *const self)
{
    const uint8_t irq_was_enabled =
        (NVIC_GetStatusIRQ(self->config->rx_irq) != 0U) ? 1U : 0U;
    uint32_t wraps;
    uint32_t flags_before;
    uint32_t flags_after;
    uint16_t remaining;

    NVIC_DisableIRQ(self->config->rx_irq);
    wraps = self->state->rx_wrap_count;
    flags_before = self->config->dma->INTFR;
    remaining = (uint16_t)self->config->rx_dma->CNTR;
    flags_after = self->config->dma->INTFR;
    if (((flags_before & self->config->rx_dma_complete_flag) == 0U) &&
        ((flags_after & self->config->rx_dma_complete_flag) != 0U))
    {
        /* 回卷恰好夹在两次寄存器采样之间时，重新取新一圈的 CNTR。 */
        remaining = (uint16_t)self->config->rx_dma->CNTR;
    }
    if (((flags_before | flags_after) &
         self->config->rx_dma_complete_flag) != 0U)
    {
        wraps++;
    }
    if (irq_was_enabled != 0U)
    {
        NVIC_EnableIRQ(self->config->rx_irq);
    }

    return (wraps * UART_FORWARD_RX_BUFFER_SIZE) +
           ((UART_FORWARD_RX_BUFFER_SIZE - remaining) &
            UART_DMA_RX_BUFFER_MASK);
}

void UART_DMA_RX_IRQ_HANDLER(void)
    __attribute__((interrupt("WCH-Interrupt-fast")));

void UART_DMA_RX_IRQ_HANDLER(void)
{
    const uint32_t flags = uart2_dma.config->dma->INTFR;

    if ((flags & uart2_dma.config->rx_dma_error_flag) != 0U)
    {
        uart2_dma.config->rx_dma->CFGR &= ~(uint32_t)DMA_CFGR1_EN;
        uart2_dma.config->dma->INTFCR =
            uart2_dma.config->rx_dma_global_flag;
        uart2_dma.state->rx_dma_fault = 1U;
        return;
    }
    if ((flags & uart2_dma.config->rx_dma_complete_flag) != 0U)
    {
        __asm volatile ("" ::: "memory");
        uart2_dma.state->rx_wrap_count++;
        uart2_dma.config->dma->INTFCR =
            uart2_dma.config->rx_dma_global_flag;
        return;
    }
    uart2_dma.config->dma->INTFCR = uart2_dma.config->rx_dma_global_flag;
}

_Static_assert(UART_FORWARD_BAUD_RATE == 115200UL,
               "UART forwarding baud rate must remain fixed at 115200");
_Static_assert(UART_FORWARD_STOP_BITS == 0U,
               "UART forwarding requires one stop bit");
_Static_assert(UART_FORWARD_PARITY == 0U,
               "UART forwarding requires no parity");
_Static_assert(UART_FORWARD_DATA_BITS == 8U,
               "UART forwarding requires eight data bits");
_Static_assert((UART_FORWARD_PERIPHERAL_CLOCK_HZ %
                UART_FORWARD_BAUD_RATE) == 0UL,
               "UART BRR must be an exact compile-time divisor");
_Static_assert(UART_DMA_BRR_VALUE ==
                   (UART_FORWARD_PERIPHERAL_CLOCK_HZ / UART_FORWARD_BAUD_RATE),
               "UART BRR must match the selected board clock exactly");
_Static_assert((UART_FORWARD_RX_BUFFER_SIZE & UART_DMA_RX_BUFFER_MASK) == 0U,
               "UART RX buffer size must be a power of two");
_Static_assert((UART_FORWARD_TX_BUFFER_SIZE & UART_DMA_TX_BUFFER_MASK) == 0U,
               "UART TX buffer size must be a power of two");
_Static_assert(sizeof(Uart2DmaState) <= 1056U,
               "UART DMA state exceeds its static RAM budget");
