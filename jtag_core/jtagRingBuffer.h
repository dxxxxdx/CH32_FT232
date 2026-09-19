#ifndef CH32_FT232_JTAG_RING_BUFFER_H
#define CH32_FT232_JTAG_RING_BUFFER_H

#include <stdint.h>

#define JTAG_RING_BUFFER_FLASH __attribute__((section(".rodata.jtag_rb")))

typedef struct JtagRingBuffer JtagRingBuffer;

typedef struct
{
    void (*clear)(const JtagRingBuffer *self);
    uint16_t (*used)(const JtagRingBuffer *self);
    uint16_t (*free)(const JtagRingBuffer *self);
    uint8_t (*front)(const JtagRingBuffer *self);
    uint8_t (*take)(const JtagRingBuffer *self);
    void (*put)(const JtagRingBuffer *self, uint8_t value);
    void (*write)(const JtagRingBuffer *self,
                  const uint8_t *data,
                  uint16_t length);
    uint16_t (*peek)(const JtagRingBuffer *self, const uint8_t **data);
    void (*consume)(const JtagRingBuffer *self, uint16_t length);
} JtagRingBufferOps;

typedef struct
{
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t used;
} JtagRingBufferState;

struct JtagRingBuffer
{
    const JtagRingBufferOps *const ops;
    JtagRingBufferState *const state;
    uint8_t *const data;
    const uint16_t mask;
};

extern const JtagRingBufferOps JtagRingBufferOps0;

#define JTAG_RING_BUFFER_DEFINE(name, size)                                \
    _Static_assert((size) >= 2U && (size) <= 32768U &&                     \
                   (((size) & ((size) - 1U)) == 0U),                      \
                   "JTAG ring capacity must be a bounded power of two"); \
    static uint8_t name##_data[size];                                     \
    static JtagRingBufferState name##_state;                               \
    static const JtagRingBuffer name JTAG_RING_BUFFER_FLASH = {            \
        .ops = &JtagRingBufferOps0,                                         \
        .state = &name##_state,                                             \
        .data = name##_data,                                                \
        .mask = (size) - 1U                                                 \
    }

#endif /* CH32_FT232_JTAG_RING_BUFFER_H */
