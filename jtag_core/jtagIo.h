#ifndef CH32_FT232_JTAG_IO_H
#define CH32_FT232_JTAG_IO_H

#include <stdint.h>

typedef struct JtagIo JtagIo;

typedef struct
{
    uint8_t (*shift_lsb)(const JtagIo *self, uint8_t data, uint8_t bits);
    uint8_t (*shift_msb)(const JtagIo *self, uint8_t data, uint8_t bits);
    uint8_t (*shift_tms)(const JtagIo *self, uint8_t data, uint8_t bits);
    void (*shift_msb_output)(const JtagIo *self, uint8_t data, uint8_t bits);
    void (*clock_program_dr32)(const JtagIo *self,
                               const uint8_t word[4],
                               uint8_t tail);
    void (*clock_erase)(const JtagIo *self);
} JtagIoOps;

struct JtagIo
{
    const JtagIoOps *const ops;
    const void *const context;
};

/* 由 GPIO_Cfg 提供的编译期板级实现。 */
extern const JtagIo JtagIo0;

#endif /* CH32_FT232_JTAG_IO_H */
