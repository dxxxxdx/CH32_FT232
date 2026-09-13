#define GPIO_CFG_IMPLEMENTATION
#include "GPIO_Cfg.h"

#include "ch32v20x.h"

#define JTAG_GPIO_FLASH __attribute__((section(".rodata.jtag_gpio")))
/* 当前 GPIO 实测 TCK 约 2 MHz。Gowin 内置 Flash 擦除要求约 160 ms
 * 连续时钟；额外留 20 ms 裕量，不能继续照搬 BL702 的 150000 脉冲。
 */
#define JTAG_FIXED_TCK_HZ             (2000000UL)
#define JTAG_GOWIN_ERASE_TIME_US      (180000UL)
#define JTAG_GOWIN_ERASE_CLOCKS       \
    ((JTAG_FIXED_TCK_HZ / 1000000UL) * JTAG_GOWIN_ERASE_TIME_US)

_Static_assert((JTAG_FIXED_TCK_HZ % 1000000UL) == 0UL,
               "fixed TCK must convert to clocks/us at compile time");

/* CH32V20x 每个 GPIO 配置占四位；模式值来自 GPIOx_CFGLR/CFGHR。 */
#define GPIO_CFG_MODE_OUTPUT_PP_50MHZ (0x03UL)
#define GPIO_CFG_MODE_INPUT_FLOATING  (0x04UL)
#define GPIO_CFG_MODE_INPUT_PULL      (0x08UL)
#define GPIO_CFG_MODE_AF_PP_50MHZ     (0x0BUL)
#define GPIO_CFG_BITS_PER_PIN         (4U)
#define GPIO_CFG_FIELD_MASK           (0x0FUL)
#define GPIO_CFG_LOW_PIN_COUNT        (8U)

typedef struct
{
    const GpioCfgPin *const tck;
    const GpioCfgPin *const tdi;
    const GpioCfgPin *const tdo;
    const GpioCfgPin *const tms;
} JtagGpioPins;

#if UART_FORWARD_ENABLED != 0U
typedef struct
{
    const GpioCfgPin *const tx;
    const GpioCfgPin *const rx;
    volatile uint32_t *const remap_register;
    const uint32_t remap_mask;
    const uint32_t remap_value;
} UartGpioRoute;
#endif

struct GpioCfg
{
    const JtagGpioPins *const jtag;
#if UART_FORWARD_ENABLED != 0U
    const UartGpioRoute *const uart;
#endif
    const GpioCfgPin *const usbd_dm;
    const GpioCfgPin *const usbd_dp;
};

static uint8_t jtag_gpio_shift_lsb(const JtagIo *self,
                                   uint8_t data,
                                   uint8_t bits);
static uint8_t jtag_gpio_shift_msb(const JtagIo *self,
                                   uint8_t data,
                                   uint8_t bits);
static uint8_t jtag_gpio_shift_tms(const JtagIo *self,
                                   uint8_t data,
                                   uint8_t bits);
static void jtag_gpio_shift_msb_output(const JtagIo *self,
                                       uint8_t data,
                                       uint8_t bits);
static void jtag_gpio_clock_program_dr32(const JtagIo *self,
                                         const uint8_t word[4],
                                         uint8_t tail);
static void jtag_gpio_clock_erase(const JtagIo *self);
static inline __attribute__((always_inline)) void jtag_gpio_edge_delay(void);
static inline void gpio_cfg_pin_write(const GpioCfgPin *pin, uint8_t value);
static inline void gpio_cfg_pin_set(const GpioCfgPin *pin);
static inline void gpio_cfg_pin_reset(const GpioCfgPin *pin);
static inline uint8_t gpio_cfg_pin_read(const GpioCfgPin *pin);
static void gpio_cfg_pin_clock_enable(const GpioCfgPin *pin);
static void gpio_cfg_pin_mode(const GpioCfgPin *pin, uint32_t mode);

static const JtagGpioPins jtag_gpio_pins JTAG_GPIO_FLASH = {
    .tck = &jtag_gpio_tck,
    .tdi = &jtag_gpio_tdi,
    .tdo = &jtag_gpio_tdo,
    .tms = &jtag_gpio_tms
};

#if UART_FORWARD_ENABLED != 0U
static const UartGpioRoute uart_gpio_route GPIO_CFG_FLASH = {
    .tx = &uart_gpio_tx,
    .rx = &uart_gpio_rx,
    .remap_register = UART_GPIO_REMAP_REGISTER,
    .remap_mask = UART_GPIO_REMAP_MASK,
    .remap_value = UART_GPIO_REMAP_VALUE
};
#endif

static const JtagIoOps jtag_gpio_ops JTAG_GPIO_FLASH = {
    .shift_lsb = jtag_gpio_shift_lsb,
    .shift_msb = jtag_gpio_shift_msb,
    .shift_tms = jtag_gpio_shift_tms,
    .shift_msb_output = jtag_gpio_shift_msb_output,
    .clock_program_dr32 = jtag_gpio_clock_program_dr32,
    .clock_erase = jtag_gpio_clock_erase
};

const GpioCfg GpioCfg0 GPIO_CFG_FLASH = {
    .jtag = &jtag_gpio_pins,
#if UART_FORWARD_ENABLED != 0U
    .uart = &uart_gpio_route,
#endif
    .usbd_dm = &usbd_gpio_dm,
    .usbd_dp = &usbd_gpio_dp
};

const JtagIo JtagIo0 JTAG_GPIO_FLASH = {
    .ops = &jtag_gpio_ops,
    .context = &jtag_gpio_pins
};

void GPIO_Cfg_Init(const GpioCfg *const self)
{
    const JtagGpioPins *const pins = self->jtag;
#if UART_FORWARD_ENABLED != 0U
    const UartGpioRoute *const uart = self->uart;
#endif

    /* 引脚对象可分布在不同端口，不在初始化器里暗藏第二份管脚映射。 */
    RCC->APB2PCENR |= RCC_AFIOEN;
    gpio_cfg_pin_clock_enable(pins->tck);
    gpio_cfg_pin_clock_enable(pins->tdi);
    gpio_cfg_pin_clock_enable(pins->tdo);
    gpio_cfg_pin_clock_enable(pins->tms);
#if UART_FORWARD_ENABLED != 0U
    gpio_cfg_pin_clock_enable(uart->tx);
    gpio_cfg_pin_clock_enable(uart->rx);
#endif
    gpio_cfg_pin_clock_enable(self->usbd_dm);
    gpio_cfg_pin_clock_enable(self->usbd_dp);
#if UART_FORWARD_ENABLED != 0U
    *uart->remap_register =
        (*uart->remap_register & ~uart->remap_mask) | uart->remap_value;

    /* RX 的 OUTDR 位选择内部上拉。TX 先以普通推挽输出高电平，再切换到
     * 所选 UART 的复用推挽，避免 UART 尚未使能时在线上制造低脉冲。
     */
    gpio_cfg_pin_set(uart->tx);
    gpio_cfg_pin_set(uart->rx);
    gpio_cfg_pin_mode(uart->tx, GPIO_CFG_MODE_OUTPUT_PP_50MHZ);
    gpio_cfg_pin_mode(uart->rx, GPIO_CFG_MODE_INPUT_PULL);
    gpio_cfg_pin_mode(uart->tx, GPIO_CFG_MODE_AF_PP_50MHZ);
#endif

    /* 先写低输出锁存，再逐脚切推挽输出，避免配置瞬间
     * 在 TCK/TMS 上产生伪上升沿。TDO 保持浮空，空闲高电平由板上拉高保证。
     */
    pins->tck->port->BCR = (uint32_t)pins->tck->mask;
    pins->tms->port->BCR = (uint32_t)pins->tms->mask;
    pins->tdi->port->BCR = (uint32_t)pins->tdi->mask;
    gpio_cfg_pin_mode(pins->tck, GPIO_CFG_MODE_OUTPUT_PP_50MHZ);
    gpio_cfg_pin_mode(pins->tms, GPIO_CFG_MODE_OUTPUT_PP_50MHZ);
    gpio_cfg_pin_mode(pins->tdi, GPIO_CFG_MODE_OUTPUT_PP_50MHZ);
    gpio_cfg_pin_mode(pins->tdo, GPIO_CFG_MODE_INPUT_FLOATING);
}

void GPIO_Cfg_UsbdPinsRelease(const GpioCfg *const self)
{
    gpio_cfg_pin_mode(self->usbd_dm, GPIO_CFG_MODE_INPUT_FLOATING);
    gpio_cfg_pin_mode(self->usbd_dp, GPIO_CFG_MODE_INPUT_FLOATING);
}

void GPIO_Cfg_UsbdPinsDriveLow(const GpioCfg *const self)
{
    gpio_cfg_pin_reset(self->usbd_dm);
    gpio_cfg_pin_reset(self->usbd_dp);
    gpio_cfg_pin_mode(self->usbd_dm, GPIO_CFG_MODE_OUTPUT_PP_50MHZ);
    gpio_cfg_pin_mode(self->usbd_dp, GPIO_CFG_MODE_OUTPUT_PP_50MHZ);
}

static inline void gpio_cfg_pin_write(const GpioCfgPin *const pin,
                                      uint8_t value)
{
    if (value != 0U)
    {
        gpio_cfg_pin_set(pin);
    }
    else
    {
        gpio_cfg_pin_reset(pin);
    }
}

static inline void gpio_cfg_pin_set(const GpioCfgPin *const pin)
{
    /* 保留 BL702 路径的 OUTDR 读改写节拍，不能换成 BSHR/BCR 改变建立时间。 */
    pin->port->OUTDR |= (uint32_t)pin->mask;
}

static inline void gpio_cfg_pin_reset(const GpioCfgPin *const pin)
{
    pin->port->OUTDR &= ~(uint32_t)pin->mask;
}

static inline uint8_t gpio_cfg_pin_read(const GpioCfgPin *const pin)
{
    return ((pin->port->INDR & (uint32_t)pin->mask) != 0U) ? 1U : 0U;
}

static void gpio_cfg_pin_clock_enable(const GpioCfgPin *const pin)
{
    RCC->APB2PCENR |= pin->port_clock;
}

static void gpio_cfg_pin_mode(const GpioCfgPin *const pin, uint32_t mode)
{
    const uint32_t field_shift =
        ((uint32_t)pin->number & (GPIO_CFG_LOW_PIN_COUNT - 1U)) *
        GPIO_CFG_BITS_PER_PIN;
    const uint32_t field_mask = GPIO_CFG_FIELD_MASK << field_shift;
    volatile uint32_t *const config =
        (pin->number < GPIO_CFG_LOW_PIN_COUNT)
            ? &pin->port->CFGLR
            : &pin->port->CFGHR;

    *config = (*config & ~field_mask) |
              ((mode & GPIO_CFG_FIELD_MASK) << field_shift);
}

static uint8_t jtag_gpio_shift_lsb(const JtagIo *const self,
                                   uint8_t data,
                                   uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        gpio_cfg_pin_reset(pins->tck);
        gpio_cfg_pin_write(pins->tdi, (uint8_t)(data & 0x01U));
        data = (uint8_t)(data >> 1U);
        reply = (uint8_t)(reply >> 1U);
        gpio_cfg_pin_set(pins->tck);
        if (gpio_cfg_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x80U;
        }
    }
    gpio_cfg_pin_reset(pins->tck);
    return reply;
}

static uint8_t jtag_gpio_shift_msb(const JtagIo *const self,
                                   uint8_t data,
                                   uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        gpio_cfg_pin_reset(pins->tck);
        gpio_cfg_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
        data = (uint8_t)(data << 1U);
        reply = (uint8_t)(reply << 1U);
        gpio_cfg_pin_set(pins->tck);
        if (gpio_cfg_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x01U;
        }
    }
    gpio_cfg_pin_reset(pins->tck);
    return reply;
}

static void jtag_gpio_shift_msb_output(const JtagIo *const self,
                                       uint8_t data,
                                       uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;

    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        gpio_cfg_pin_reset(pins->tck);
        gpio_cfg_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
        data = (uint8_t)(data << 1U);
        gpio_cfg_pin_set(pins->tck);
    }
    gpio_cfg_pin_reset(pins->tck);
}

static uint8_t jtag_gpio_shift_tms(const JtagIo *const self,
                                   uint8_t data,
                                   uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    gpio_cfg_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        gpio_cfg_pin_reset(pins->tck);
        gpio_cfg_pin_write(pins->tms, (uint8_t)(data & 0x01U));
        data = (uint8_t)(data >> 1U);
        reply = (uint8_t)(reply >> 1U);
        gpio_cfg_pin_set(pins->tck);
        if (gpio_cfg_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x80U;
        }
    }
    gpio_cfg_pin_reset(pins->tck);
    return reply;
}

static void jtag_gpio_clock_program_dr32(const JtagIo *const self,
                                         const uint8_t word[4],
                                         uint8_t tail)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    GPIO_TypeDef *const port = pins->tck->port;
    const uint32_t tck_mask = (uint32_t)pins->tck->mask;
    const uint32_t tms_mask = (uint32_t)pins->tms->mask;
    const uint32_t tdi_mask = (uint32_t)pins->tdi->mask;
    const uint32_t output_base = port->OUTDR & ~(tck_mask | tms_mask | tdi_mask);
    uint32_t output_low = output_base;

    /* 三个输出由 GPIO_Cfg.h 静态约束在同一端口；合并写保持
     * Gowin 24/7/1 位之间无空档。
     */
    for (uint8_t bit_index = 0U; bit_index < 32U; bit_index++)
    {
        uint8_t tdi;

        if (bit_index < 24U)
        {
            const uint8_t byte_index = (uint8_t)(bit_index >> 3U);
            const uint8_t bit_in_byte = (uint8_t)(7U - (bit_index & 0x07U));
            tdi = (uint8_t)((word[byte_index] >> bit_in_byte) & 0x01U);
        }
        else if (bit_index < 31U)
        {
            tdi = (uint8_t)((word[3] >> (31U - bit_index)) & 0x01U);
        }
        else
        {
            tdi = (uint8_t)((tail >> 7U) & 0x01U);
        }

        output_low = output_base;
        if (tdi != 0U)
        {
            output_low |= tdi_mask;
        }
        if ((bit_index == 31U) && ((tail & 0x01U) != 0U))
        {
            output_low |= tms_mask;
        }

        port->OUTDR = output_low;
        jtag_gpio_edge_delay();
        port->OUTDR = output_low | tck_mask;
        jtag_gpio_edge_delay();
    }
    port->OUTDR = output_low;
}

static void jtag_gpio_clock_erase(const JtagIo *const self)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;

    /* 擦除窗口只由 Gowin Flash 控制流调用一次；普通 GPIO 连续输出，
     * 不依赖 PWM 引脚复用，也不受主机声明的虚假 MPSSE 档位影响。
     */
    gpio_cfg_pin_reset(pins->tms);
    gpio_cfg_pin_reset(pins->tdi);
    for (uint32_t clock = 0U; clock < JTAG_GOWIN_ERASE_CLOCKS; clock++)
    {
        gpio_cfg_pin_reset(pins->tck);
        jtag_gpio_edge_delay();
        gpio_cfg_pin_set(pins->tck);
        jtag_gpio_edge_delay();
    }
    gpio_cfg_pin_reset(pins->tck);
}

static inline __attribute__((always_inline)) void jtag_gpio_edge_delay(void)
{
    __asm volatile ("nop\n\tnop" ::: "memory");
}
