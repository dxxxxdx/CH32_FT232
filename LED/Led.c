#include "Led.h"

/* 模式值来自 CH32V20x GPIOx_CFGLR/CFGHR 的 MODE/CNF 四位字段。 */
#define LED_GPIO_MODE_OUTPUT_PP_50MHZ (0x03UL)
#define LED_GPIO_MODE_OUTPUT_OD_50MHZ (0x07UL)
#define LED_GPIO_BITS_PER_PIN         (4U)
#define LED_GPIO_FIELD_MASK           (0x0FUL)
#define LED_GPIO_LOW_PIN_COUNT        (8U)
/* WCH SysTick：HCLK、自重载、无中断计数。 */
#define LED_SYSTICK_FREE_RUNNING      (0x0000000DUL)

static uint8_t led_timebase_started;

static void led_write(const Led *self, uint8_t active);
static void led_configure_output(const Led *self);
static uint32_t led_time_now(void);
static void led_timebase_start(void);

void Led_Init(const Led *const self)
{
    LedState *const state = self->state;

    RCC->APB2PCENR |= self->config->port_clock | RCC_AFIOEN;
    *self->config->remap_register =
        (*self->config->remap_register & ~self->config->remap_mask) |
        self->config->remap_value;

    /* 先写入灭灯电平再切输出，避免初始化瞬间闪一下。 */
    if (self->config->active_level == LED_ACTIVE_LOW)
    {
        self->config->port->BSHR = (uint32_t)self->config->mask;
    }
    else
    {
        self->config->port->BCR = (uint32_t)self->config->mask;
    }
    led_configure_output(self);

    state->last_toggle = 0U;
    state->service_armed = 0U;
    state->active = 0U;
    state->initialized = 1U;
}

void Led_On(const Led *const self)
{
    if (self->state->initialized != 0U)
    {
        led_write(self, 1U);
    }
}

void Led_Off(const Led *const self)
{
    if (self->state->initialized != 0U)
    {
        led_write(self, 0U);
    }
}

void Led_Toggle(const Led *const self)
{
    if (self->state->initialized != 0U)
    {
        led_write(self, (self->state->active == 0U) ? 1U : 0U);
    }
}

void Led_Service(const Led *const self)
{
    LedState *const state = self->state;
    uint32_t now;

    /* 未初始化和 period=0 都是接口明确允许的可选配置。 */
    if ((state->initialized == 0U) ||
        (self->config->service_period_ticks == 0U))
    {
        return;
    }
    if (led_timebase_started == 0U)
    {
        led_timebase_start();
    }

    now = led_time_now();
    if (state->service_armed == 0U)
    {
        state->last_toggle = now;
        state->service_armed = 1U;
        return;
    }
    if ((uint32_t)(now - state->last_toggle) <
        self->config->service_period_ticks)
    {
        return;
    }

    state->last_toggle = now;
    Led_Toggle(self);
}

static void led_write(const Led *const self, uint8_t active)
{
    const uint8_t drive_high =
        ((active != 0U) ==
         (self->config->active_level == LED_ACTIVE_HIGH)) ? 1U : 0U;

    if (drive_high != 0U)
    {
        self->config->port->BSHR = (uint32_t)self->config->mask;
    }
    else
    {
        self->config->port->BCR = (uint32_t)self->config->mask;
    }
    self->state->active = (active != 0U) ? 1U : 0U;
}

static void led_configure_output(const Led *const self)
{
    const uint32_t field_shift =
        ((uint32_t)self->config->number & (LED_GPIO_LOW_PIN_COUNT - 1U)) *
        LED_GPIO_BITS_PER_PIN;
    const uint32_t field_mask = LED_GPIO_FIELD_MASK << field_shift;
    const uint32_t mode =
        (self->config->output_type == LED_OUTPUT_OPEN_DRAIN)
            ? LED_GPIO_MODE_OUTPUT_OD_50MHZ
            : LED_GPIO_MODE_OUTPUT_PP_50MHZ;
    volatile uint32_t *const gpio_config =
        (self->config->number < LED_GPIO_LOW_PIN_COUNT)
            ? &self->config->port->CFGLR
            : &self->config->port->CFGHR;

    *gpio_config = (*gpio_config & ~field_mask) | (mode << field_shift);
}

static uint32_t led_time_now(void)
{
    const volatile uint32_t *const systick_low =
        (const volatile uint32_t *)(uintptr_t)&SysTick->CNT;

    return *systick_low;
}

static void led_timebase_start(void)
{
    SysTick->CTLR = 0U;
    SysTick->CNT = 0U;
    SysTick->CMP = UINT64_MAX;
    SysTick->CTLR = LED_SYSTICK_FREE_RUNNING;
    led_timebase_started = 1U;
}
