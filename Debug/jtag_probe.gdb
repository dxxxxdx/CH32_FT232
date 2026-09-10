# CH32V203 JTAG 现场检查。所有外设址均是固定寄存器地址，
# 即使当前停在静态 inline 函数中，也不依赖局部变量是否被 GCC 保留。

set pagination off

define jtag-gpio
    set $apb2pcenr = *(unsigned int *)0x40021018
    set $gpioa_cfghr = *(unsigned int *)0x40010804
    set $gpioa_indr = *(unsigned int *)0x40010808
    set $gpioa_outdr = *(unsigned int *)0x4001080c
    set $gpiob_cfghr = *(unsigned int *)0x40010c04
    set $gpiob_indr = *(unsigned int *)0x40010c08
    set $gpiob_outdr = *(unsigned int *)0x40010c0c

    printf "[JTAG GPIO] APB2PCENR=0x%08x IOPA=%u IOPB=%u\n", $apb2pcenr, (($apb2pcenr >> 2) & 1), (($apb2pcenr >> 3) & 1)
    printf "[JTAG GPIO] GPIOA CFGHR=0x%08x INDR=0x%04x OUTDR=0x%04x\n", $gpioa_cfghr, ($gpioa_indr & 0xffff), ($gpioa_outdr & 0xffff)
    printf "[JTAG GPIO] GPIOB CFGHR=0x%08x INDR=0x%04x OUTDR=0x%04x\n", $gpiob_cfghr, ($gpiob_indr & 0xffff), ($gpiob_outdr & 0xffff)
    printf "[JTAG PINS] TCK(PB13)=%u TDI(PB15)=%u TMS(PB14)=%u TDO(PA8)=%u\n", (($gpiob_outdr >> 13) & 1), (($gpiob_outdr >> 15) & 1), (($gpiob_outdr >> 14) & 1), (($gpioa_indr >> 8) & 1)

    if (($apb2pcenr & 0x0000000c) != 0x0000000c)
        printf "[JTAG WARN] GPIOA/GPIOB APB2 clock is not fully enabled\n"
    end
    if (($gpioa_cfghr & 0x0000000f) != 0x00000004)
        printf "[JTAG WARN] PA8 is not floating input, PA8 CFG=0x%x\n", ($gpioa_cfghr & 0x0f)
    end
    if (($gpiob_cfghr & 0xfff00000) != 0x33300000)
        printf "[JTAG WARN] PB13/PB14/PB15 are not all 50MHz push-pull outputs\n"
    end
end
document jtag-gpio
Read and decode CH32V203 JTAG GPIO registers. Safe to run at any breakpoint.
end

define jtag-mpsse
    printf "[MPSSE] state@%p phase=%u opcode=0x%02x argc=%u remaining=%u\n", &JTAGManager0_state, JTAGManager0_state.mpsse.phase, JTAGManager0_state.mpsse.opcode, JTAGManager0_state.mpsse.argument_count, JTAGManager0_state.mpsse.remaining
    printf "[MPSSE] RX used=%u read=%u write=%u; TX used=%u read=%u write=%u\n", JTAGManager0_rx_state.used, JTAGManager0_rx_state.read_pos, JTAGManager0_rx_state.write_pos, JTAGManager0_tx_state.used, JTAGManager0_tx_state.read_pos, JTAGManager0_tx_state.write_pos
    printf "[GOWIN] collect=%u ready=%u overflow=%u erase_candidate=%u erase_suppress=%u\n", JTAGManager0_state.gowin.transfer_collecting, JTAGManager0_state.gowin.transfer_ready, JTAGManager0_state.gowin.transfer_overflow, JTAGManager0_state.gowin.long_clock_candidate, JTAGManager0_state.gowin.long_clock_suppress
    printf "[GOWIN] ir_pending=%u ir_low7=0x%02x program=%u word_stage=%u word=%02x%02x%02x%02x\n", JTAGManager0_state.gowin.ir_pending, JTAGManager0_state.gowin.ir_low7, JTAGManager0_state.gowin.program_active, JTAGManager0_state.gowin.program_word_stage, JTAGManager0_state.gowin.program_word[0], JTAGManager0_state.gowin.program_word[1], JTAGManager0_state.gowin.program_word[2], JTAGManager0_state.gowin.program_word[3]
end
document jtag-mpsse
Print the single owner MPSSE parser and RX/TX queue state.
end

define jtag-check
    printf "\n[JTAG STOP] PC=%p\n", $pc
    info line *$pc
    jtag-gpio
    jtag-mpsse
    printf "[JTAG FRAME]\n"
    info args
    info locals
    x/8i $pc
end
document jtag-check
Print GPIO registers, MPSSE state, current arguments/locals and nearby code.
Run this after stopping in jtag_gpio_shift_lsb/jtag_gpio_shift_tms.
end

printf "JTAG probe loaded. Run: jtag-check\n"
