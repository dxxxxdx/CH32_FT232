#ifndef CH32_FT232_22PINOUT_H
#define CH32_FT232_22PINOUT_H

/* 仅由 BoardConfig.h 选择；22PINOUT 没有运行灯。 */
#define BOARD_JTAG_OUTPUT_PORT       GPIOA
#define BOARD_JTAG_OUTPUT_PORT_CLOCK RCC_IOPAEN
#define BOARD_JTAG_TCK_PIN           (6U)
#define BOARD_JTAG_TMS_PIN           (7U)
#define BOARD_JTAG_TDI_PIN           (5U)
#define BOARD_JTAG_TDO_PORT          GPIOA
#define BOARD_JTAG_TDO_PORT_CLOCK    RCC_IOPAEN
#define BOARD_JTAG_TDO_PIN           (4U)

#define BOARD_USB_PORT         GPIOA
#define BOARD_USB_PORT_CLOCK   RCC_IOPAEN
#define BOARD_USB_DM_PIN       (11U)
#define BOARD_USB_DP_PIN       (12U)
#define BOARD_USB_CLOCK_SOURCE RCC_USBCLKSource_PLLCLK_Div3

#define BOARD_HAS_UART (1U)
#include "Ch32V203Usart1Pb67.h"

#define BOARD_HAS_RUN_LED (0U)

#endif /* CH32_FT232_22PINOUT_H */
