#ifndef CH32_FT232_BOARD_CONFIG_H
#define CH32_FT232_BOARD_CONFIG_H

/* 未定义的选择宏按未选处理；必须明确且只选择一个板型，禁止默认引脚。 */
#ifndef BOARD_20PINOUT
#define BOARD_20PINOUT (0U)
#endif
#ifndef BOARD_22PINOUT
#define BOARD_22PINOUT (0U)
#endif
#ifndef BOARD_USER
#define BOARD_USER (0U)
#endif
#if ((BOARD_20PINOUT != 0U) && (BOARD_20PINOUT != 1U)) || \
    ((BOARD_22PINOUT != 0U) && (BOARD_22PINOUT != 1U)) || \
    ((BOARD_USER != 0U) && (BOARD_USER != 1U))
#error "Board selectors must be 0 or 1"
#endif
#if (BOARD_20PINOUT + BOARD_22PINOUT + BOARD_USER) != 1U
#error "Select exactly one board: BOARD_20PINOUT, BOARD_22PINOUT or BOARD_USER"
#elif BOARD_20PINOUT
#include "20pinout.h"
#elif BOARD_22PINOUT
#include "22pinout.h"
#else
#include "UserBoard.h"
#endif

/* 整字输出依赖 TCK/TMS/TDI 同端口，板型用一个输出端口明确这个约束。 */
#if !defined(BOARD_JTAG_OUTPUT_PORT) || !defined(BOARD_JTAG_OUTPUT_PORT_CLOCK) || \
    !defined(BOARD_JTAG_TCK_PIN) || !defined(BOARD_JTAG_TMS_PIN) || \
    !defined(BOARD_JTAG_TDI_PIN) || !defined(BOARD_JTAG_TDO_PORT) || \
    !defined(BOARD_JTAG_TDO_PORT_CLOCK) || !defined(BOARD_JTAG_TDO_PIN)
#error "Board must define all JTAG pins and GPIO clocks"
#endif
#if (BOARD_JTAG_TCK_PIN >= 16U) || (BOARD_JTAG_TMS_PIN >= 16U) || \
    (BOARD_JTAG_TDI_PIN >= 16U) || (BOARD_JTAG_TDO_PIN >= 16U)
#error "JTAG GPIO pin numbers must be 0..15"
#endif
#if (BOARD_JTAG_TCK_PIN == BOARD_JTAG_TMS_PIN) || \
    (BOARD_JTAG_TCK_PIN == BOARD_JTAG_TDI_PIN) || \
    (BOARD_JTAG_TMS_PIN == BOARD_JTAG_TDI_PIN)
#error "JTAG output pins must be distinct"
#endif
#if !defined(BOARD_USB_PORT) || !defined(BOARD_USB_PORT_CLOCK) || \
    !defined(BOARD_USB_DM_PIN) || !defined(BOARD_USB_DP_PIN) || \
    !defined(BOARD_USB_CLOCK_SOURCE)
#error "Board must define USB pins and clocks"
#endif
#if (BOARD_USB_DM_PIN >= 16U) || (BOARD_USB_DP_PIN >= 16U) || \
    (BOARD_USB_DM_PIN == BOARD_USB_DP_PIN)
#error "USB GPIO pins must be distinct and within 0..15"
#endif
#if !defined(BOARD_HAS_UART) || !defined(BOARD_HAS_RUN_LED)
#error "Board must declare UART and run LED availability"
#endif
#if ((BOARD_HAS_UART != 0U) && (BOARD_HAS_UART != 1U)) || \
    ((BOARD_HAS_RUN_LED != 0U) && (BOARD_HAS_RUN_LED != 1U))
#error "Board resource availability must be 0 or 1"
#endif

#if BOARD_HAS_UART
#if !defined(BOARD_UART_GPIO_PORT) || !defined(BOARD_UART_GPIO_PORT_CLOCK) || \
    !defined(BOARD_UART_TX_PIN) || !defined(BOARD_UART_RX_PIN) || \
    !defined(BOARD_UART_REMAP_REGISTER) || !defined(BOARD_UART_REMAP_MASK) || \
    !defined(BOARD_UART_REMAP_VALUE)
#error "Board must define UART pins and remap settings"
#endif
#if (BOARD_UART_TX_PIN >= 16U) || (BOARD_UART_RX_PIN >= 16U) || \
    (BOARD_UART_TX_PIN == BOARD_UART_RX_PIN)
#error "UART GPIO pins must be distinct and within 0..15"
#endif
#if !defined(BOARD_UART_PERIPHERAL) || !defined(BOARD_UART_PERIPHERAL_CLOCK_HZ) || \
    !defined(BOARD_UART_CLOCK_REGISTER) || !defined(BOARD_UART_RESET_REGISTER) || \
    !defined(BOARD_UART_CLOCK_MASK) || !defined(BOARD_UART_RESET_MASK)
#error "Board must define UART peripheral and clocks"
#endif
#if !defined(BOARD_UART_DMA) || !defined(BOARD_UART_DMA_CLOCK_REGISTER) || \
    !defined(BOARD_UART_DMA_CLOCK_MASK) || !defined(BOARD_UART_RX_DMA) || \
    !defined(BOARD_UART_TX_DMA) || !defined(BOARD_UART_RX_DMA_IRQ) || \
    !defined(BOARD_UART_RX_DMA_IRQ_HANDLER) || !defined(BOARD_UART_RX_DMA_GLOBAL_FLAG) || \
    !defined(BOARD_UART_RX_DMA_DONE_FLAG) || !defined(BOARD_UART_RX_DMA_ERROR_FLAG) || \
    !defined(BOARD_UART_TX_DMA_GLOBAL_FLAG) || !defined(BOARD_UART_TX_DMA_DONE_FLAG) || \
    !defined(BOARD_UART_TX_DMA_ERROR_FLAG)
#error "Board must define UART DMA channels, IRQ and flags"
#endif
#endif

#if BOARD_HAS_RUN_LED
#if !defined(BOARD_RUN_LED_PORT) || !defined(BOARD_RUN_LED_PORT_CLOCK) || \
    !defined(BOARD_RUN_LED_PIN_NUMBER) || !defined(BOARD_RUN_LED_ACTIVE_LEVEL) || \
    !defined(BOARD_RUN_LED_OUTPUT_TYPE) || !defined(BOARD_RUN_LED_REMAP_REGISTER) || \
    !defined(BOARD_RUN_LED_REMAP_MASK) || !defined(BOARD_RUN_LED_REMAP_VALUE)
#error "Board must define run LED pin, polarity and remap settings"
#endif
#if BOARD_RUN_LED_PIN_NUMBER >= 16U
#error "Run LED GPIO pin number must be 0..15"
#endif
#endif

#include "BoardFeatures.h"

#endif /* CH32_FT232_BOARD_CONFIG_H */
