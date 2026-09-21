#include "boardtype/BoardGpio.h"
#include "boardtype/BoardConfig.h"
#include "jtagIo.h"

#include "ch32v20x.h"
#include "jtagTrace.h"
#if JTAG_ACM_TRACE_ENABLED
#include "SystemTimebase.h"
#endif

#define JTAG_GPIO_FLASH __attribute__((section(".rodata.jtag_gpio")))
/* 保留 BL702 的连续时钟路径和 volatile 半周期循环，擦除拍数增加到
 * 参考值 150000 的四倍，为 CH32 留出时间余量；不改变每拍速度。
 * 当前 144 MHz / -Og 的旧 ELF 每拍约 55 条指令，600000 拍按一条
 * 指令一周期估算约 229 ms；这不是引脚实测，编译变化后需重新核对。
 */
#define JTAG_GOWIN_ERASE_CLOCKS (600000U)
#define JTAG_GOWIN_EDGE_DELAY_LOOPS (2U)

/* CH32V20x 每个 GPIO 配置占四位；模式值来自 GPIOx_CFGLR/CFGHR。 */
#define BOARD_GPIO_MODE_OUTPUT_PP_50MHZ (0x03UL)
#define BOARD_GPIO_MODE_INPUT_FLOATING  (0x04UL)
#define BOARD_GPIO_MODE_INPUT_PULL      (0x08UL)
#define BOARD_GPIO_MODE_AF_PP_50MHZ     (0x0BUL)
#define BOARD_GPIO_BITS_PER_PIN         (4U)
#define BOARD_GPIO_FIELD_MASK           (0x0FUL)
#define BOARD_GPIO_LOW_PIN_COUNT        (8U)

#define BOARD_GPIO_FLASH __attribute__((section(".rodata.gpio_cfg")))

typedef struct
{
    GPIO_TypeDef *const port;
    const uint32_t port_clock;
    const uint16_t mask;
    const uint8_t number;
} BoardGpioPin;

/* 初始化器只装配板型给出的常量，不保存第二份引脚配置。 */
#define BOARD_GPIO_PIN(port_, port_clock_, number_)                  \
    {                                                               \
        .port = (port_),                                             \
        .port_clock = (uint32_t)(port_clock_),                        \
        .mask = (uint16_t)(1UL << (number_)),                          \
        .number = (uint8_t)(number_)                                  \
    }

static const BoardGpioPin jtag_gpio_tck BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_JTAG_OUTPUT_PORT, BOARD_JTAG_OUTPUT_PORT_CLOCK, BOARD_JTAG_TCK_PIN);
static const BoardGpioPin jtag_gpio_tdi BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_JTAG_OUTPUT_PORT, BOARD_JTAG_OUTPUT_PORT_CLOCK, BOARD_JTAG_TDI_PIN);
static const BoardGpioPin jtag_gpio_tdo BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_JTAG_TDO_PORT, BOARD_JTAG_TDO_PORT_CLOCK, BOARD_JTAG_TDO_PIN);
static const BoardGpioPin jtag_gpio_tms BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_JTAG_OUTPUT_PORT, BOARD_JTAG_OUTPUT_PORT_CLOCK, BOARD_JTAG_TMS_PIN);
#if UART_FORWARD_ENABLED != 0U
static const BoardGpioPin uart_gpio_tx BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_UART_GPIO_PORT, BOARD_UART_GPIO_PORT_CLOCK, BOARD_UART_TX_PIN);
static const BoardGpioPin uart_gpio_rx BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_UART_GPIO_PORT, BOARD_UART_GPIO_PORT_CLOCK, BOARD_UART_RX_PIN);
#endif
static const BoardGpioPin usbd_gpio_dm BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_USB_PORT, BOARD_USB_PORT_CLOCK, BOARD_USB_DM_PIN);
static const BoardGpioPin usbd_gpio_dp BOARD_GPIO_FLASH =
    BOARD_GPIO_PIN(BOARD_USB_PORT, BOARD_USB_PORT_CLOCK, BOARD_USB_DP_PIN);

typedef struct
{
    const BoardGpioPin *const tck;
    const BoardGpioPin *const tdi;
    const BoardGpioPin *const tdo;
    const BoardGpioPin *const tms;
} JtagGpioPins;

#if UART_FORWARD_ENABLED != 0U
typedef struct
{
    const BoardGpioPin *const tx;
    const BoardGpioPin *const rx;
    volatile uint32_t *const remap_register;
    const uint32_t remap_mask;
    const uint32_t remap_value;
} UartGpioRoute;
#endif

struct BoardGpio
{
    const JtagGpioPins *const jtag;
#if UART_FORWARD_ENABLED != 0U
    const UartGpioRoute *const uart;
#endif
    const BoardGpioPin *const usbd_dm;
    const BoardGpioPin *const usbd_dp;
};

static uint8_t jtag_gpio_shift_lsb(const JtagIo *self,
                                   uint8_t data,
                                   uint16_t bits);
static uint8_t jtag_gpio_shift_msb(const JtagIo *self,
                                   uint8_t data,
                                   uint16_t bits);
static uint8_t jtag_gpio_shift_tms(const JtagIo *self,
                                   uint8_t data,
                                   uint16_t bits);
static void jtag_gpio_shift_msb_output(const JtagIo *self,
                                       uint8_t data,
                                       uint16_t bits);
static void jtag_gpio_clock_program_dr32(const JtagIo *self,
                                         const uint8_t word[4],
                                         uint8_t tail);
static void jtag_gpio_clock_erase(const JtagIo *self);
static void jtag_gpio_clock_program_idle(const JtagIo *self, uint32_t clocks);
static void jtag_gpio_clock_run_test(const JtagIo *self, uint32_t clocks);
static inline __attribute__((always_inline)) void jtag_gpio_edge_delay(void);
static inline void board_gpio_pin_write(const BoardGpioPin *pin, uint8_t value);
static inline void board_gpio_pin_set(const BoardGpioPin *pin);
static inline void board_gpio_pin_reset(const BoardGpioPin *pin);
static inline uint8_t board_gpio_pin_read(const BoardGpioPin *pin);
static void board_gpio_pin_clock_enable(const BoardGpioPin *pin);
static void board_gpio_pin_mode(const BoardGpioPin *pin, uint32_t mode);

static const JtagGpioPins jtag_gpio_pins JTAG_GPIO_FLASH = {
    .tck = &jtag_gpio_tck,
    .tdi = &jtag_gpio_tdi,
    .tdo = &jtag_gpio_tdo,
    .tms = &jtag_gpio_tms
};

#if UART_FORWARD_ENABLED != 0U
static const UartGpioRoute uart_gpio_route BOARD_GPIO_FLASH = {
    .tx = &uart_gpio_tx,
    .rx = &uart_gpio_rx,
    .remap_register = BOARD_UART_REMAP_REGISTER,
    .remap_mask = BOARD_UART_REMAP_MASK,
    .remap_value = BOARD_UART_REMAP_VALUE
};
#endif

static const JtagIoOps jtag_gpio_ops JTAG_GPIO_FLASH = {
    .shift_lsb = jtag_gpio_shift_lsb,
    .shift_msb = jtag_gpio_shift_msb,
    .shift_tms = jtag_gpio_shift_tms,
    .shift_msb_output = jtag_gpio_shift_msb_output,
    .clock_program_dr32 = jtag_gpio_clock_program_dr32,
    .clock_erase = jtag_gpio_clock_erase,
    .clock_program_idle = jtag_gpio_clock_program_idle
};

const BoardGpio BoardGpio0 BOARD_GPIO_FLASH = {
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

void BoardGpio_Init(const BoardGpio *const self)
{
    const JtagGpioPins *const pins = self->jtag;
#if UART_FORWARD_ENABLED != 0U
    const UartGpioRoute *const uart = self->uart;
#endif

    /* 引脚对象可分布在不同端口，不在初始化器里暗藏第二份管脚映射。 */
    RCC->APB2PCENR |= RCC_AFIOEN;
    board_gpio_pin_clock_enable(pins->tck);
    board_gpio_pin_clock_enable(pins->tdi);
    board_gpio_pin_clock_enable(pins->tdo);
    board_gpio_pin_clock_enable(pins->tms);
#if UART_FORWARD_ENABLED != 0U
    board_gpio_pin_clock_enable(uart->tx);
    board_gpio_pin_clock_enable(uart->rx);
#endif
    board_gpio_pin_clock_enable(self->usbd_dm);
    board_gpio_pin_clock_enable(self->usbd_dp);
#if UART_FORWARD_ENABLED != 0U
    *uart->remap_register =
        (*uart->remap_register & ~uart->remap_mask) | uart->remap_value;

    /* RX 的 OUTDR 位选择内部上拉。TX 先以普通推挽输出高电平，再切换到
     * 所选 UART 的复用推挽，避免 UART 尚未使能时在线上制造低脉冲。
     */
    board_gpio_pin_set(uart->tx);
    board_gpio_pin_set(uart->rx);
    board_gpio_pin_mode(uart->tx, BOARD_GPIO_MODE_OUTPUT_PP_50MHZ);
    board_gpio_pin_mode(uart->rx, BOARD_GPIO_MODE_INPUT_PULL);
    board_gpio_pin_mode(uart->tx, BOARD_GPIO_MODE_AF_PP_50MHZ);
#endif

    /* 先写低输出锁存，再逐脚切推挽输出，避免配置瞬间
     * 在 TCK/TMS 上产生伪上升沿。TDO 保持浮空，空闲高电平由板上拉高保证。
     */
    pins->tck->port->BCR = (uint32_t)pins->tck->mask;
    pins->tms->port->BCR = (uint32_t)pins->tms->mask;
    pins->tdi->port->BCR = (uint32_t)pins->tdi->mask;
    board_gpio_pin_mode(pins->tck, BOARD_GPIO_MODE_OUTPUT_PP_50MHZ);
    board_gpio_pin_mode(pins->tms, BOARD_GPIO_MODE_OUTPUT_PP_50MHZ);
    board_gpio_pin_mode(pins->tdi, BOARD_GPIO_MODE_OUTPUT_PP_50MHZ);
    board_gpio_pin_mode(pins->tdo, BOARD_GPIO_MODE_INPUT_FLOATING);
}

void BoardGpio_UsbdPinsRelease(const BoardGpio *const self)
{
    board_gpio_pin_mode(self->usbd_dm, BOARD_GPIO_MODE_INPUT_FLOATING);
    board_gpio_pin_mode(self->usbd_dp, BOARD_GPIO_MODE_INPUT_FLOATING);
}

void BoardGpio_UsbdPinsDriveLow(const BoardGpio *const self)
{
    board_gpio_pin_reset(self->usbd_dm);
    board_gpio_pin_reset(self->usbd_dp);
    board_gpio_pin_mode(self->usbd_dm, BOARD_GPIO_MODE_OUTPUT_PP_50MHZ);
    board_gpio_pin_mode(self->usbd_dp, BOARD_GPIO_MODE_OUTPUT_PP_50MHZ);
}

static inline void board_gpio_pin_write(const BoardGpioPin *const pin,
                                      uint8_t value)
{
    if (value != 0U)
    {
        board_gpio_pin_set(pin);
    }
    else
    {
        board_gpio_pin_reset(pin);
    }
}

static inline void board_gpio_pin_set(const BoardGpioPin *const pin)
{
    /* 保留 BL702 路径的 OUTDR 读改写节拍，不能换成 BSHR/BCR 改变建立时间。 */
    pin->port->OUTDR |= (uint32_t)pin->mask;
}

static inline void board_gpio_pin_reset(const BoardGpioPin *const pin)
{
    pin->port->OUTDR &= ~(uint32_t)pin->mask;
}

static inline uint8_t board_gpio_pin_read(const BoardGpioPin *const pin)
{
    return ((pin->port->INDR & (uint32_t)pin->mask) != 0U) ? 1U : 0U;
}

static void board_gpio_pin_clock_enable(const BoardGpioPin *const pin)
{
    RCC->APB2PCENR |= pin->port_clock;
}

static void board_gpio_pin_mode(const BoardGpioPin *const pin, uint32_t mode)
{
    const uint32_t field_shift =
        ((uint32_t)pin->number & (BOARD_GPIO_LOW_PIN_COUNT - 1U)) *
        BOARD_GPIO_BITS_PER_PIN;
    const uint32_t field_mask = BOARD_GPIO_FIELD_MASK << field_shift;
    volatile uint32_t *const config =
        (pin->number < BOARD_GPIO_LOW_PIN_COUNT)
            ? &pin->port->CFGLR
            : &pin->port->CFGHR;

    *config = (*config & ~field_mask) |
              ((mode & BOARD_GPIO_FIELD_MASK) << field_shift);
}

static uint8_t jtag_gpio_shift_lsb(const JtagIo *const self,
                                   uint8_t data,
                                   uint16_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    for (uint16_t bit = 0U; bit < bits; bit++)
    {
        board_gpio_pin_reset(pins->tck);
        board_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x01U));
        data = (uint8_t)(data >> 1U);
        reply = (uint8_t)(reply >> 1U);
        board_gpio_pin_set(pins->tck);
        if (board_gpio_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x80U;
        }
    }
    board_gpio_pin_reset(pins->tck);
    return reply;
}

static uint8_t jtag_gpio_shift_msb(const JtagIo *const self,
                                   uint8_t data,
                                   uint16_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    for (uint16_t bit = 0U; bit < bits; bit++)
    {
        board_gpio_pin_reset(pins->tck);
        board_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
        data = (uint8_t)(data << 1U);
        reply = (uint8_t)(reply << 1U);
        board_gpio_pin_set(pins->tck);
        if (board_gpio_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x01U;
        }
    }
    board_gpio_pin_reset(pins->tck);
    return reply;
}

static void jtag_gpio_shift_msb_output(const JtagIo *const self,
                                       uint8_t data,
                                       uint16_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;

    for (uint16_t bit = 0U; bit < bits; bit++)
    {
        board_gpio_pin_reset(pins->tck);
        board_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
        data = (uint8_t)(data << 1U);
        board_gpio_pin_set(pins->tck);
    }
    board_gpio_pin_reset(pins->tck);
}

static uint8_t jtag_gpio_shift_tms(const JtagIo *const self,
                                   uint8_t data,
                                   uint16_t bits)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;
    uint8_t reply = 0U;

    board_gpio_pin_write(pins->tdi, (uint8_t)(data & 0x80U));
    for (uint16_t bit = 0U; bit < bits; bit++)
    {
        board_gpio_pin_reset(pins->tck);
        board_gpio_pin_reset(pins->tck);
        board_gpio_pin_write(pins->tms, (uint8_t)(data & 0x01U));
        data = (uint8_t)(data >> 1U);
        reply = (uint8_t)(reply >> 1U);
        board_gpio_pin_set(pins->tck);
        if (board_gpio_pin_read(pins->tdo) != 0U)
        {
            reply |= 0x80U;
        }
    }
    board_gpio_pin_reset(pins->tck);
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

    /* 三个输出由 BoardConfig.h 静态约束在同一端口；合并写保持
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
    JtagTrace_EraseBegin(&JtagTrace0, JTAG_GOWIN_ERASE_CLOCKS);
#if JTAG_ACM_TRACE_ENABLED
    const uint32_t started = SystemTimebase_Now(&SystemTimebase0);
#endif
    jtag_gpio_clock_run_test(self, JTAG_GOWIN_ERASE_CLOCKS);
#if JTAG_ACM_TRACE_ENABLED
    const uint32_t elapsed = SystemTimebase_Now(&SystemTimebase0) - started;
    JtagTrace_EraseDone(&JtagTrace0, JTAG_GOWIN_ERASE_CLOCKS, elapsed);
#endif
}

static void jtag_gpio_clock_program_idle(const JtagIo *const self, uint32_t clocks)
{
#if JTAG_ACM_TRACE_ENABLED
    const uint32_t started = SystemTimebase_Now(&SystemTimebase0);
#endif
    jtag_gpio_clock_run_test(self, clocks);
#if JTAG_ACM_TRACE_ENABLED
    const uint32_t elapsed = SystemTimebase_Now(&SystemTimebase0) - started;
    JtagTrace_ProgramIdle(&JtagTrace0, clocks, elapsed);
#endif
}

static void jtag_gpio_clock_run_test(const JtagIo *const self, uint32_t clocks)
{
    const JtagGpioPins *const pins = (const JtagGpioPins *)self->context;

    /* 擦除和每字编程等待共用同一连续边沿循环；主循环已屏蔽中断。
     * TMS 恒低，循环内无 USB、命令解析、逐字节返回或诊断观察。
     */
    board_gpio_pin_reset(pins->tms);
    board_gpio_pin_reset(pins->tdi);
    for (uint32_t bit = 0U; bit < clocks; bit++)
    {
        board_gpio_pin_reset(pins->tck);
        jtag_gpio_edge_delay();
        board_gpio_pin_set(pins->tck);
        jtag_gpio_edge_delay();
    }
    board_gpio_pin_reset(pins->tck);
}

static inline __attribute__((always_inline)) void jtag_gpio_edge_delay(void)
{
    for (volatile uint32_t delay = 0U; delay < JTAG_GOWIN_EDGE_DELAY_LOOPS; delay++)
    {
        __asm volatile ("nop" ::: "memory");
    }
}
