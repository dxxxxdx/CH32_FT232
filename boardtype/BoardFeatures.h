#ifndef CH32_FT232_BOARD_FEATURES_H
#define CH32_FT232_BOARD_FEATURES_H

#include "JtagTraceConfig.h"

/* 功能开关只决定是否使用板上资源，不能另选一套引脚。 */
#ifdef UART_FORWARD_PORT
#error "UART_FORWARD_PORT was removed; select hardware in boardtype and use UART_FORWARD_ENABLED"
#endif
#ifndef UART_FORWARD_ENABLED
#define UART_FORWARD_ENABLED (0U)
#endif
#ifndef RUN_LED_ENABLED
#define RUN_LED_ENABLED BOARD_HAS_RUN_LED
#endif

#if (UART_FORWARD_ENABLED != 0U) && (UART_FORWARD_ENABLED != 1U)
#error "UART_FORWARD_ENABLED must be 0 or 1"
#endif
#if (RUN_LED_ENABLED != 0U) && (RUN_LED_ENABLED != 1U)
#error "RUN_LED_ENABLED must be 0 or 1"
#endif
#if UART_FORWARD_ENABLED && !BOARD_HAS_UART
#error "Selected board has no UART pins"
#endif
#if RUN_LED_ENABLED && !BOARD_HAS_RUN_LED
#error "Selected board has no run LED"
#endif
#if JTAG_ACM_TRACE_ENABLED && UART_FORWARD_ENABLED
#error "JTAG ACM trace requires UART forwarding disabled"
#endif

#define USB_CDC_ENABLED ((UART_FORWARD_ENABLED || JTAG_ACM_TRACE_ENABLED) ? 1U : 0U)

/* UART 和 CDC 共用固定线编码；缓冲、搬运粒度不参与硬件路由选择。 */
#define UART_FORWARD_BAUD_RATE       (115200UL)
#define UART_FORWARD_STOP_BITS       (0U)
#define UART_FORWARD_PARITY          (0U)
#define UART_FORWARD_DATA_BITS       (8U)
#define UART_FORWARD_RX_BUFFER_SIZE  (512U)
#define UART_FORWARD_TX_BUFFER_SIZE  (512U)
/* CH32V203 的 USB PMA 布局要求 CDC 数据端点保持 16 字节。 */
#define UART_FORWARD_COPY_CHUNK_SIZE (16U)
#define RUN_LED_SERVICE_PERIOD_TICKS (72000000UL)

#endif /* CH32_FT232_BOARD_FEATURES_H */
