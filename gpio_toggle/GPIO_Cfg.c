#define GPIO_CFG_IMPLEMENTATION
#include "GPIO_Cfg.h"

#include "ch32v20x.h"

#define JTAG_GPIO_FLASH __attribute__((section(".rodata.jtag_gpio")))
#define JTAG_GOWIN_ERASE_CLOCKS (150000UL)

/* CFGHR 每个引脚占四位。0x3=50 MHz 通用推挽输出，0x4=浮空输入。
 * PB13~PB15 对应 bit20~31；PA8 对应 bit0~3。
 */
#define JTAG_GPIOB_CFG_MASK   (0xFFF00000UL)
#define JTAG_GPIOB_OUTPUT_CFG (0x33300000UL)
#define JTAG_GPIOA_CFG_MASK   (0x0000000FUL)
#define JTAG_GPIOA_INPUT_CFG  (0x00000004UL)

typedef struct
{
    const JtagGpioPin *const tck;
    const JtagGpioPin *const tdi;
    const JtagGpioPin *const tdo;
    const JtagGpioPin *const tms;
} JtagGpioPins;

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
static inline void jtag_gpio_pin_write(const JtagGpioPin *pin, uint8_t value);
static inline void jtag_gpio_pin_set(const JtagGpioPin *pin);
static inline void jtag_gpio_pin_reset(const JtagGpioPin *pin);
static inline uint8_t jtag_gpio_pin_read(const JtagGpioPin *pin);

static const JtagGpioPins jtag_gpio_pins JTAG_GPIO_FLASH = {
    .tck = &jtag_gpio_tck,
    .tdi = &jtag_gpio_tdi,
    .tdo = &jtag_gpio_tdo,
    .tms = &jtag_gpio_tms
};

static const JtagIoOps jtag_gpio_ops JTAG_GPIO_FLASH = {
    .shift_lsb = jtag_gpio_shift_lsb,
    .shift_msb = jtag_gpio_shift_msb,
    .shift_tms = jtag_gpio_shift_tms,
    .shift_msb_output = jtag_gpio_shift_msb_output,
    .clock_program_dr32 = jtag_gpio_clock_program_dr32,
    .clock_erase = jtag_gpio_clock_erase
};

const JtagIo JtagIo0 JTAG_GPIO_FLASH = {
    .ops = &jtag_gpio_ops,
    .context = &jtag_gpio_pins
};

void GPIO_Cfg_Init(const JtagIo *const self)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    const uint32_t output_mask = (uint32_t)pins->tck->mask |
                                 (uint32_t)pins->tms->mask |
                                 (uint32_t)pins->tdi->mask;

    /* 先写低输出锁存，再切推挽输出，避免配置瞬间在 TCK/TMS 上产生伪上升沿。 */
    RCC->APB2PCENR |= (RCC_IOPAEN | RCC_IOPBEN);
    pins->tck->port->BCR = output_mask;
    pins->tck->port->CFGHR =
        (pins->tck->port->CFGHR & ~JTAG_GPIOB_CFG_MASK) |
        JTAG_GPIOB_OUTPUT_CFG;
    pins->tdo->port->CFGHR =
        (pins->tdo->port->CFGHR & ~JTAG_GPIOA_CFG_MASK) |
        JTAG_GPIOA_INPUT_CFG;
}

static inline void jtag_gpio_pin_write(const JtagGpioPin *const pin,
                                       uint8_t value)
{
    if (value != 0U)
    {
        jtag_gpio_pin_set(pin);
    }
    else
    {
        jtag_gpio_pin_reset(pin);
    }
}

static inline void jtag_gpio_pin_set(const JtagGpioPin *const pin)
{
    /* 保留 BL702 路径的 OUTDR 读改写节拍，不能换成 BSHR/BCR 改变建立时间。 */
    pin->port->OUTDR |= (uint32_t)pin->mask;
}

static inline void jtag_gpio_pin_reset(const JtagGpioPin *const pin)
{
    pin->port->OUTDR &= ~(uint32_t)pin->mask;
}

static inline uint8_t jtag_gpio_pin_read(const JtagGpioPin *const pin)
{
    return ((pin->port->INDR & (uint32_t)pin->mask) != 0U) ? 1U : 0U;
}

static uint8_t jtag_gpio_shift_lsb(const JtagIo *const self,
                                   uint8_t data,
                                   uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        jtag_gpio_pin_reset(pins->tck);
        jtag_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x01U));
        data = (uint8_t)(data >> 1U);
        reply = (uint8_t)(reply >> 1U);
        jtag_gpio_pin_set(pins->tck);
        if (jtag_gpio_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x80U;
        }
    }
    jtag_gpio_pin_reset(pins->tck);
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
        jtag_gpio_pin_reset(pins->tck);
        jtag_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
        data = (uint8_t)(data << 1U);
        reply = (uint8_t)(reply << 1U);
        jtag_gpio_pin_set(pins->tck);
        if (jtag_gpio_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x01U;
        }
    }
    jtag_gpio_pin_reset(pins->tck);
    return reply;
}

static void jtag_gpio_shift_msb_output(const JtagIo *const self,
                                       uint8_t data,
                                       uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;

    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        jtag_gpio_pin_reset(pins->tck);
        jtag_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
        data = (uint8_t)(data << 1U);
        jtag_gpio_pin_set(pins->tck);
    }
    jtag_gpio_pin_reset(pins->tck);
}

static uint8_t jtag_gpio_shift_tms(const JtagIo *const self,
                                   uint8_t data,
                                   uint8_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    jtag_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
    for (uint8_t bit = 0U; bit < bits; bit++)
    {
        jtag_gpio_pin_reset(pins->tck);
        jtag_gpio_pin_write(pins->tms, (uint8_t)(data & 0x01U));
        data = (uint8_t)(data >> 1U);
        reply = (uint8_t)(reply >> 1U);
        jtag_gpio_pin_set(pins->tck);
        if (jtag_gpio_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x80U;
        }
    }
    jtag_gpio_pin_reset(pins->tck);
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

    /* 三个输出静态配置在 GPIOB；合并写保持 Gowin 24/7/1 位之间无空档。 */
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

    jtag_gpio_pin_reset(pins->tms);
    jtag_gpio_pin_reset(pins->tdi);
    for (uint32_t clock = 0U; clock < JTAG_GOWIN_ERASE_CLOCKS; clock++)
    {
        jtag_gpio_pin_reset(pins->tck);
        jtag_gpio_edge_delay();
        jtag_gpio_pin_set(pins->tck);
        jtag_gpio_edge_delay();
    }
    jtag_gpio_pin_reset(pins->tck);
}

static inline __attribute__((always_inline)) void jtag_gpio_edge_delay(void)
{
    __asm volatile ("nop\n\tnop" ::: "memory");
}
