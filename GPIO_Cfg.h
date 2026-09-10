#ifndef CH32_FT232_GPIO_CFG_H
#define CH32_FT232_GPIO_CFG_H

#include <stdint.h>

#include "jtagIo.h"

#ifdef GPIO_CFG_IMPLEMENTATION

#include "ch32v20x.h"

#define JTAG_GPIO_CONFIG_FLASH \
    __attribute__((section(".rodata.jtag_gpio")))

typedef struct
{
    GPIO_TypeDef *const port;
    const uint16_t mask;
} JtagGpioPin;

/* 板级四线配置集中在这里；修改引脚时不需要进入翻转实现。 */
static const JtagGpioPin jtag_gpio_tck JTAG_GPIO_CONFIG_FLASH = {
    .port = GPIOB,
    .mask = (uint16_t)GPIO_Pin_13
};

static const JtagGpioPin jtag_gpio_tdi JTAG_GPIO_CONFIG_FLASH = {
    .port = GPIOB,
    .mask = (uint16_t)GPIO_Pin_15
};

static const JtagGpioPin jtag_gpio_tdo JTAG_GPIO_CONFIG_FLASH = {
    .port = GPIOA,
    .mask = (uint16_t)GPIO_Pin_8
};

static const JtagGpioPin jtag_gpio_tms JTAG_GPIO_CONFIG_FLASH = {
    .port = GPIOB,
    .mask = (uint16_t)GPIO_Pin_14
};

#endif /* GPIO_CFG_IMPLEMENTATION */

void GPIO_Cfg_Init(const JtagIo *self);

#endif /* CH32_FT232_GPIO_CFG_H */
