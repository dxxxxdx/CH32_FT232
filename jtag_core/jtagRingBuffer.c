#include "jtagRingBuffer.h"

#define JTAG_RING_BUFFER_MASK (JTAG_RING_BUFFER_SIZE - 1U)

_Static_assert((JTAG_RING_BUFFER_SIZE & JTAG_RING_BUFFER_MASK) == 0U,
               "JTAG ring buffer size must be a power of two");
_Static_assert(sizeof(JtagRingBufferState) <= 2056U,
               "JTAG ring buffer exceeds its static RAM budget");

static void jtag_ring_clear(const JtagRingBuffer *self);
static uint16_t jtag_ring_used(const JtagRingBuffer *self);
static uint16_t jtag_ring_free(const JtagRingBuffer *self);
static uint8_t jtag_ring_front(const JtagRingBuffer *self);
static uint8_t jtag_ring_take(const JtagRingBuffer *self);
static void jtag_ring_put(const JtagRingBuffer *self, uint8_t value);
static void jtag_ring_write(const JtagRingBuffer *self,
                            const uint8_t *data,
                            uint16_t length);
static uint16_t jtag_ring_peek(const JtagRingBuffer *self,
                               const uint8_t **data);
static void jtag_ring_consume(const JtagRingBuffer *self, uint16_t length);

const JtagRingBufferOps JtagRingBufferOps0 JTAG_RING_BUFFER_FLASH = {
    .clear = jtag_ring_clear,
    .used = jtag_ring_used,
    .free = jtag_ring_free,
    .front = jtag_ring_front,
    .take = jtag_ring_take,
    .put = jtag_ring_put,
    .write = jtag_ring_write,
    .peek = jtag_ring_peek,
    .consume = jtag_ring_consume
};

static void jtag_ring_clear(const JtagRingBuffer *const self)
{
    /* used 清零即归还整个静态队列，旧字节不可见，不浪费启动时间擦数组。 */
    self->state->read_pos = 0U;
    self->state->write_pos = 0U;
    self->state->used = 0U;
}

static uint16_t jtag_ring_used(const JtagRingBuffer *const self)
{
    return self->state->used;
}

static uint16_t jtag_ring_free(const JtagRingBuffer *const self)
{
    return (uint16_t)(JTAG_RING_BUFFER_SIZE - self->state->used);
}

static uint8_t jtag_ring_front(const JtagRingBuffer *const self)
{
    return self->state->data[self->state->read_pos];
}

static uint8_t jtag_ring_take(const JtagRingBuffer *const self)
{
    const uint8_t value = self->state->data[self->state->read_pos];

    self->state->read_pos =
        (uint16_t)((self->state->read_pos + 1U) & JTAG_RING_BUFFER_MASK);
    self->state->used--;
    return value;
}

static void jtag_ring_put(const JtagRingBuffer *const self, uint8_t value)
{
    self->state->data[self->state->write_pos] = value;
    self->state->write_pos =
        (uint16_t)((self->state->write_pos + 1U) & JTAG_RING_BUFFER_MASK);
    self->state->used++;
}

static void jtag_ring_write(const JtagRingBuffer *const self,
                            const uint8_t *const data,
                            uint16_t length)
{
    for (uint16_t index = 0U; index < length; index++)
    {
        jtag_ring_put(self, data[index]);
    }
}

static uint16_t jtag_ring_peek(const JtagRingBuffer *const self,
                               const uint8_t **const data)
{
    uint16_t contiguous;

    if (self->state->used == 0U)
    {
        *data = (const uint8_t *)0;
        return 0U;
    }

    contiguous = (uint16_t)(JTAG_RING_BUFFER_SIZE - self->state->read_pos);
    if (contiguous > self->state->used)
    {
        contiguous = self->state->used;
    }
    *data = &self->state->data[self->state->read_pos];
    return contiguous;
}

static void jtag_ring_consume(const JtagRingBuffer *const self, uint16_t length)
{
    /* length 来自最近一次 Peek，由上层维持不越界的内部契约。 */
    self->state->read_pos =
        (uint16_t)((self->state->read_pos + length) & JTAG_RING_BUFFER_MASK);
    self->state->used = (uint16_t)(self->state->used - length);
}
