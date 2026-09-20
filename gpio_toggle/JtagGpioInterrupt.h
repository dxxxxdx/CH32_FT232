#ifndef CH32_FT232_JTAG_GPIO_INTERRUPT_H
#define CH32_FT232_JTAG_GPIO_INTERRUPT_H

#include <stdint.h>

/* QingKeV4 手册 8.3：GINTENR(CSR 0x800) 映射 MIE/MPIE；掩码与
 * WCH Core/core_riscv.h 的 __disable_irq 一致。只保存并恢复这两个位，
 * 不覆盖 mstatus 其它字段；NMI 和异常不属于可屏蔽中断。
 */
#define JTAG_GPIO_INTERRUPT_MASK (0x88U)

static inline __attribute__((always_inline))
uint32_t JtagGpioInterrupt_Save(void)
{
    uint32_t previous;

    __asm volatile ("csrrc %0, 0x800, %1\n\tfence.i"
                    : "=r" (previous)
                    : "r" (JTAG_GPIO_INTERRUPT_MASK)
                    : "memory");
    return previous & JTAG_GPIO_INTERRUPT_MASK;
}

static inline __attribute__((always_inline))
void JtagGpioInterrupt_Restore(uint32_t previous)
{
    __asm volatile ("csrs 0x800, %0"
                    : : "r" (previous & JTAG_GPIO_INTERRUPT_MASK) : "memory");
}

#endif
