#ifndef CH32_FT232_LED_H
#define CH32_FT232_LED_H

#include <stdint.h>

#include "ch32v20x.h"

#define LED_FLASH __attribute__((section(".rodata.led")))

typedef enum
{
    LED_ACTIVE_HIGH = 0U,
    LED_ACTIVE_LOW = 1U
} LedActiveLevel;

typedef enum
{
    LED_OUTPUT_PUSH_PULL = 0U,
    LED_OUTPUT_OPEN_DRAIN = 1U
} LedOutputType;

typedef struct
{
    GPIO_TypeDef *const port;
    const uint32_t port_clock;
    const uint32_t service_period_ticks;
    const uint16_t mask;
    const uint8_t number;
    const uint8_t active_level;
    const uint8_t output_type;
} LedConfig;

typedef struct
{
    uint32_t last_toggle;
    uint8_t initialized;
    uint8_t service_armed;
    uint8_t active;
} LedState;

typedef struct
{
    const LedConfig *const config;
    LedState *const state;
} Led;

/* period_ticks=0 时只响应 On/Off/Toggle，不做周期翻转。
 * 对象可以只实例化不初始化，此时所有服务都不会碰对应 GPIO。
 */
#define LED_DEFINE(name_, port_, port_clock_, number_, active_, output_,      \
                   period_ticks_)                                            \
    _Static_assert((number_) < 16U, "LED GPIO number must be 0..15");        \
    _Static_assert(((active_) == LED_ACTIVE_HIGH) ||                          \
                       ((active_) == LED_ACTIVE_LOW),                          \
                   "LED active level is invalid");                           \
    _Static_assert(((output_) == LED_OUTPUT_PUSH_PULL) ||                     \
                       ((output_) == LED_OUTPUT_OPEN_DRAIN),                   \
                   "LED output type is invalid");                            \
    static LedState name_##_state;                                            \
    static const LedConfig name_##_config LED_FLASH = {                       \
        .port = (port_),                                                       \
        .port_clock = (uint32_t)(port_clock_),                                 \
        .service_period_ticks = (uint32_t)(period_ticks_),                     \
        .mask = (uint16_t)(1UL << (number_)),                                  \
        .number = (uint8_t)(number_),                                          \
        .active_level = (uint8_t)(active_),                                    \
        .output_type = (uint8_t)(output_)                                      \
    };                                                                         \
    const Led name_ LED_FLASH = {                                              \
        .config = &name_##_config,                                             \
        .state = &name_##_state                                                \
    }

void Led_Init(const Led *self);
void Led_On(const Led *self);
void Led_Off(const Led *self);
void Led_Toggle(const Led *self);
void Led_Service(const Led *self);

#endif /* CH32_FT232_LED_H */
