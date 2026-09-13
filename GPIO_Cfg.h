#ifndef CH32_FT232_GPIO_CFG_H
#define CH32_FT232_GPIO_CFG_H

#include <stdint.h>

#include "UartForwardConfig.h"
#include "jtagIo.h"

typedef struct GpioCfg GpioCfg;

#ifdef GPIO_CFG_IMPLEMENTATION

#include "ch32v20x.h"

#define GPIO_CFG_FLASH __attribute__((section(".rodata.gpio_cfg")))

typedef struct
{
    GPIO_TypeDef *const port;
    const uint32_t port_clock;
    const uint16_t mask;
    const uint8_t number;
} GpioCfgPin;

#define GPIO_CFG_PIN(port_, port_clock_, number_)                    \
    {                                                               \
        .port = (port_),                                            \
        .port_clock = (uint32_t)(port_clock_),                      \
        .mask = (uint16_t)(1UL << (number_)),                       \
        .number = (uint8_t)(number_)                                \
    }

/* TCK/TDI/TMS 必须位于同一端口，clock_program_dr32 才能用一次
 * OUTDR 写保证边沿连续。20pinout 的整组映射来自板级配置头。
 */
#if BOARD_20PINOUT != 0U
#define JTAG_TCK_PIN_CFG BOARD_20PINOUT_JTAG_TCK_CFG
#define JTAG_TDI_PIN_CFG BOARD_20PINOUT_JTAG_TDI_CFG
#define JTAG_TDO_PIN_CFG BOARD_20PINOUT_JTAG_TDO_CFG
#define JTAG_TMS_PIN_CFG BOARD_20PINOUT_JTAG_TMS_CFG
#else
#define JTAG_TCK_PIN_CFG GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 4U)
#define JTAG_TDI_PIN_CFG GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 5U)
#define JTAG_TDO_PIN_CFG GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 6U)
#define JTAG_TMS_PIN_CFG GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 7U)
#endif

#if UART_FORWARD_ENABLED != 0U

#if UART_FORWARD_PORT == UART_FORWARD_PORT_PA23

#define UART_TX_PIN_CFG  GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 2U)
#define UART_RX_PIN_CFG  GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 3U)
#define UART_GPIO_REMAP_REGISTER (&AFIO->PCFR1)
#define UART_GPIO_REMAP_MASK     ((uint32_t)AFIO_PCFR1_USART2_REMAP)
#define UART_GPIO_REMAP_VALUE    (0U)

#elif UART_FORWARD_PORT == UART_FORWARD_PORT_PB67

#define UART_TX_PIN_CFG  GPIO_CFG_PIN(GPIOB, RCC_IOPBEN, 6U)
#define UART_RX_PIN_CFG  GPIO_CFG_PIN(GPIOB, RCC_IOPBEN, 7U)
#define UART_GPIO_REMAP_REGISTER (&AFIO->PCFR1)
#define UART_GPIO_REMAP_MASK     ((uint32_t)AFIO_PCFR1_USART1_REMAP)
#define UART_GPIO_REMAP_VALUE    ((uint32_t)AFIO_PCFR1_USART1_REMAP)

#endif

#endif /* UART_FORWARD_ENABLED */

static const GpioCfgPin jtag_gpio_tck GPIO_CFG_FLASH = JTAG_TCK_PIN_CFG;
static const GpioCfgPin jtag_gpio_tdi GPIO_CFG_FLASH = JTAG_TDI_PIN_CFG;
static const GpioCfgPin jtag_gpio_tdo GPIO_CFG_FLASH = JTAG_TDO_PIN_CFG;
static const GpioCfgPin jtag_gpio_tms GPIO_CFG_FLASH = JTAG_TMS_PIN_CFG;
#if UART_FORWARD_ENABLED != 0U
static const GpioCfgPin uart_gpio_tx GPIO_CFG_FLASH = UART_TX_PIN_CFG;
static const GpioCfgPin uart_gpio_rx GPIO_CFG_FLASH = UART_RX_PIN_CFG;
#endif

/* USB FS 固定使用 PA11/PA12，连接和软断开的引脚操作仍由
 * GPIO_Cfg 执行，USB 协议层不再持有引脚常量。
 */
static const GpioCfgPin usbd_gpio_dm GPIO_CFG_FLASH =
    GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 11U);

static const GpioCfgPin usbd_gpio_dp GPIO_CFG_FLASH =
    GPIO_CFG_PIN(GPIOA, RCC_IOPAEN, 12U);

#endif /* GPIO_CFG_IMPLEMENTATION */

extern const GpioCfg GpioCfg0;

void GPIO_Cfg_Init(const GpioCfg *self);
void GPIO_Cfg_UsbdPinsRelease(const GpioCfg *self);
void GPIO_Cfg_UsbdPinsDriveLow(const GpioCfg *self);

#endif /* CH32_FT232_GPIO_CFG_H */
