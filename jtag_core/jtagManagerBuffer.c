#include "jtagManager.h"

#define JTAG_RX_PACKET_SIZE (64U)
#define JTAG_GOWIN_HEADER_SEARCH_SIZE (32U)
#define JTAG_GOWIN_PAGE_TRANSFER_SIZE (1754U)

_Static_assert(JTAG_MANAGER_BUFFER_SIZE >= JTAG_GOWIN_PAGE_TRANSFER_SIZE,
               "JTAG RX must hold one complete Gowin page transfer");

static const uint8_t jtag_gowin_page_header[] JTAG_MANAGER_FLASH = {
    0x4BU, 0x03U, 0x03U, 0x1BU, 0x06U, 0x71U
};

static uint8_t jtag_packet_has_gowin_page_header(const uint8_t *data,
                                                  uint16_t length);

JtagRxPacketResult JTAGManager_RxWritePacket(const JTAGManager *const self,
                                             const uint8_t *const data,
                                             uint16_t length,
                                             uint8_t short_packet)
{
    const JtagRingBuffer *const rx = self->config->rx;
    JtagGowinState *const gowin = &self->state->gowin;

    if ((length == 0U) || (length > JTAG_RX_PACKET_SIZE))
    {
        return JTAG_RX_PACKET_INVALID_LENGTH;
    }
    if (data == (const uint8_t *)0)
    {
        return JTAG_RX_PACKET_INVALID_DATA;
    }
    if (gowin->transfer_overflow != 0U)
    {
        return JTAG_RX_PACKET_TRANSFER_OVERFLOW;
    }
    if (gowin->transfer_ready != 0U)
    {
        return JTAG_RX_PACKET_BACKPRESSURE;
    }
    if (length > rx->ops->free(rx))
    {
        if (gowin->transfer_collecting != 0U)
        {
            gowin->transfer_collecting = 0U;
            gowin->transfer_ready = 0U;
            gowin->transfer_overflow = 1U;
            return JTAG_RX_PACKET_TRANSFER_OVERFLOW;
        }
        return JTAG_RX_PACKET_BACKPRESSURE;
    }

    if ((gowin->transfer_collecting == 0U) && (rx->ops->used(rx) == 0U) &&
        (self->state->mpsse.phase == JTAG_MPSSE_COMMAND) &&
        (jtag_packet_has_gowin_page_header(data, length) != 0U))
    {
        gowin->transfer_collecting = 1U;
    }

    rx->ops->write(rx, data, length);

    if ((gowin->transfer_collecting != 0U) && (short_packet != 0U))
    {
        gowin->transfer_collecting = 0U;
        gowin->transfer_ready = 1U;
    }
    return JTAG_RX_PACKET_ACCEPTED;
}

static uint8_t jtag_packet_has_gowin_page_header(const uint8_t *const data,
                                                  uint16_t length)
{
    uint16_t search_length = length;

    if (search_length > JTAG_GOWIN_HEADER_SEARCH_SIZE)
    {
        search_length = JTAG_GOWIN_HEADER_SEARCH_SIZE;
    }
    for (uint16_t offset = 0U;
         (uint16_t)(offset + sizeof(jtag_gowin_page_header)) <= search_length;
         offset++)
    {
        uint8_t matches = 1U;

        for (uint16_t index = 0U; index < sizeof(jtag_gowin_page_header); index++)
        {
            if (data[offset + index] != jtag_gowin_page_header[index])
            {
                matches = 0U;
                break;
            }
        }
        if (matches != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

uint16_t JTAGManager_TxPeek(const JTAGManager *const self,
                            const uint8_t **const data)
{
    return self->config->tx->ops->peek(self->config->tx, data);
}

void JTAGManager_TxConsume(const JTAGManager *const self, uint16_t length)
{
    self->config->tx->ops->consume(self->config->tx, length);
}
