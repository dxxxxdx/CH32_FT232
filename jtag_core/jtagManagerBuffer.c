#include "jtagManager.h"

#define JTAG_RX_PACKET_SIZE (64U)

JtagRxPacketResult JTAGManager_RxWritePacket(const JTAGManager *const self,
                                             const uint8_t *const data,
                                             uint16_t length)
{
    const JtagRingBuffer *const rx = self->config->rx;

    if ((length == 0U) || (length > JTAG_RX_PACKET_SIZE))
    {
        return JTAG_RX_PACKET_INVALID_LENGTH;
    }
    if (data == (const uint8_t *)0)
    {
        return JTAG_RX_PACKET_INVALID_DATA;
    }
    if (length > rx->ops->free(rx))
    {
        return JTAG_RX_PACKET_BACKPRESSURE;
    }

    /* USB批次并不等于Gowin页。收包调度由service负责，跨批次的MPSSE
     * 相位和DR32暂存仍只属于manager，满队列不覆盖未执行的字节。
     */
    rx->ops->write(rx, data, length);
    return JTAG_RX_PACKET_ACCEPTED;
}

uint16_t JTAGManager_RxUsed(const JTAGManager *const self)
{
    return self->config->rx->ops->used(self->config->rx);
}

uint16_t JTAGManager_RxFree(const JTAGManager *const self)
{
    return self->config->rx->ops->free(self->config->rx);
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
