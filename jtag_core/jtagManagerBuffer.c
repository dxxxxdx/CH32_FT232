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

    /* USB transfer 的长度由主机批处理策略决定，不能拿它当 Gowin 页边界。
     * 64 字节包持续进入解析器；跨包的 MPSSE 相位和 DR32 暂存均由 manager
     * 自己持有，因此 3313 字节的高云批次不需要占用同尺寸 RAM。
     */
    rx->ops->write(rx, data, length);
    return JTAG_RX_PACKET_ACCEPTED;
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
