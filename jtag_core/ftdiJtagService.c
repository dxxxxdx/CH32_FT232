#include "ftdiJtagService.h"
#include "jtagTrace.h"

/* 对齐 BL702 的 collecting/ready/received 生命周期，只保存一个相位。 */
typedef enum
{
    FTDI_JTAG_RX_IDLE = 0U,
    FTDI_JTAG_RX_COLLECTING,
    FTDI_JTAG_RX_READY,
    FTDI_JTAG_RX_FAULT,
    FTDI_JTAG_RX_SEQUENCE_FAULT
} FtdiJtagRxPhase;

typedef enum
{
    FTDI_JTAG_PUMP_IDLE = 0,
    FTDI_JTAG_PUMP_PROGRESS,
    FTDI_JTAG_PUMP_BATCH_FULL,
    FTDI_JTAG_PUMP_WAIT
} FtdiJtagPumpResult;

static FtdiJtagServiceResult ftdi_jtag_handle_events(const FtdiJtagService *self);
static FtdiJtagPumpResult ftdi_jtag_pump_port_rx(const FtdiJtagService *self);
static FtdiJtagPumpResult ftdi_jtag_pump_port_tx(const FtdiJtagService *self);
static uint8_t ftdi_jtag_has_page_header(const uint8_t *data, uint16_t length);
static FtdiJtagServiceResult ftdi_jtag_service_locked(const FtdiJtagService *self);

FTDI_JTAG_SERVICE_DEFINE(ftdi_jtag_service, MpssePort0, JTAGManager0);

void FtdiJtagService0_Init(void)
{
    FtdiJtagService_Init(&ftdi_jtag_service);
}

void FtdiJtagService0_Poll(void)
{
    const FtdiJtagServiceResult result =
        FtdiJtagService_Service(&ftdi_jtag_service);

    ftdi_jtag_service.state->last_result = (uint8_t)result;
    if (result >= FTDI_JTAG_SERVICE_PORT_RX_LENGTH_FAULT)
    {
        ftdi_jtag_service.state->sticky_fault = (uint8_t)result;
        JtagTrace_Fault(&JtagTrace0, (uint8_t)result);
    }

    switch (result)
    {
    case FTDI_JTAG_SERVICE_IDLE:
    case FTDI_JTAG_SERVICE_PROGRESS:
    case FTDI_JTAG_SERVICE_WAIT_CONFIGURATION:
    case FTDI_JTAG_SERVICE_BACKPRESSURE:
    case FTDI_JTAG_SERVICE_TX_OVERFLOW:
    case FTDI_JTAG_SERVICE_SEQUENCE_FAULT:
        /* 外部 MPSSE/transfer 错误保持明确状态，等待主机 reset 重新同步。 */
        break;
    case FTDI_JTAG_SERVICE_PORT_RX_LENGTH_FAULT:
    case FTDI_JTAG_SERVICE_PORT_RX_OVERWRITE_FAULT:
#if JTAG_ACM_TRACE_ENABLED
        /* 诊断构建保留故障且停止本轮服务，让主循环有机会输出故障日志。 */
        break;
#endif
    default:
        /* 64 字节数据 port 不可能合法地产生这两种结果。 */
        __builtin_trap();
    }
}

FtdiJtagServiceResult FtdiJtagService0_LastResult(void)
{
    return (FtdiJtagServiceResult)ftdi_jtag_service.state->last_result;
}

void FtdiJtagService_Init(const FtdiJtagService *const self)
{
    MpssePortEvents events;

    JTAGManager_Init(self->config->jtag);
    self->config->port->ops->get_events(self->config->port, &events);
    self->state->seen_bus_reset = events.bus_reset;
    self->state->seen_sio_reset = events.sio_reset;
    self->state->seen_host_rx_purge = events.host_rx_purge;
    self->state->seen_host_tx_purge = events.host_tx_purge;
    self->state->last_result = FTDI_JTAG_SERVICE_IDLE;
    self->state->sticky_fault = FTDI_JTAG_SERVICE_IDLE;
    self->state->rx_phase = FTDI_JTAG_RX_IDLE;
    self->config->port->ops->enable(self->config->port);
}

FtdiJtagServiceResult FtdiJtagService_Service(const FtdiJtagService *const self)
{
    const uint8_t irq_token =
        self->config->port->ops->interrupt_lock(self->config->port);
    const FtdiJtagServiceResult result = ftdi_jtag_service_locked(self);

    self->config->port->ops->interrupt_unlock(self->config->port, irq_token);
    return result;
}

static FtdiJtagServiceResult ftdi_jtag_service_locked(const FtdiJtagService *const self)
{
    FtdiJtagServiceResult result = ftdi_jtag_handle_events(self);
    FtdiJtagPumpResult rx_result;
    FtdiJtagPumpResult tx_result;

    if (result != FTDI_JTAG_SERVICE_IDLE)
    {
        return result;
    }
    if (self->config->port->ops->is_configured(self->config->port) == 0U)
    {
        return FTDI_JTAG_SERVICE_WAIT_CONFIGURATION;
    }
    if (self->state->rx_phase == FTDI_JTAG_RX_FAULT)
    {
        return FTDI_JTAG_SERVICE_TX_OVERFLOW;
    }
    if (self->state->rx_phase == FTDI_JTAG_RX_SEQUENCE_FAULT)
    {
        return FTDI_JTAG_SERVICE_SEQUENCE_FAULT;
    }

    /* BL702 收到 OUT 后先执行，received 为真时连真实回复也不向 IN 提交。
     * 锁覆盖事件检查、邮箱搬运、整批 GPIO 和发送决策，避免 reset/新 OUT
     * 在判断与提交之间插入。收集等待时每次只搬一个包，立即恢复中断。
     */
    rx_result = ftdi_jtag_pump_port_rx(self);
    if (self->state->rx_phase == FTDI_JTAG_RX_COLLECTING)
    {
        return (rx_result == FTDI_JTAG_PUMP_PROGRESS)
                   ? FTDI_JTAG_SERVICE_PROGRESS : FTDI_JTAG_SERVICE_IDLE;
    }
    if (self->state->rx_phase == FTDI_JTAG_RX_READY)
    {
#if JTAG_ACM_TRACE_ENABLED
        const uint16_t batch_length = JTAGManager_RxUsed(self->config->jtag);
#endif
        const JtagServiceResult manager_result = JTAGManager_Service(self->config->jtag);
        JtagTrace_Batch(&JtagTrace0, batch_length, (uint8_t)manager_result);

        if (manager_result == JTAG_SERVICE_TX_OVERFLOW)
        {
            self->state->rx_phase = FTDI_JTAG_RX_FAULT;
            return FTDI_JTAG_SERVICE_TX_OVERFLOW;
        }
        if (manager_result == JTAG_SERVICE_SEQUENCE_FAULT)
        {
            self->state->rx_phase = FTDI_JTAG_RX_SEQUENCE_FAULT;
            return FTDI_JTAG_SERVICE_SEQUENCE_FAULT;
        }
        /* 包内没有预算让出。最后一个邮箱直到整个批次执行完才释放并重开
         * EP2；中途缺命令后缀只保留解析相位，与 BL702 的跨包状态一致。
         */
        self->state->rx_phase = FTDI_JTAG_RX_IDLE;
        if (rx_result == FTDI_JTAG_PUMP_BATCH_FULL)
        {
            /* 与 BL702 容量分支一样，触发执行的下一包尚未并入当前批次。
             * 保留这个 USB 邮箱，下一轮从新批次接收它，不能当旧包丢掉。
             */
            return FTDI_JTAG_SERVICE_PROGRESS;
        }
        self->config->port->ops->rx_consume(self->config->port);
    }

    tx_result = ftdi_jtag_pump_port_tx(self);
    if (tx_result == FTDI_JTAG_PUMP_IDLE)
    {
        /* 只有无接收事务且真实回复队列为空，才进入 BL702 的空状态包路径。 */
        self->config->port->ops->service(self->config->port);
    }
    if ((rx_result == FTDI_JTAG_PUMP_PROGRESS) || (tx_result == FTDI_JTAG_PUMP_PROGRESS))
    {
        return FTDI_JTAG_SERVICE_PROGRESS;
    }
    return (tx_result == FTDI_JTAG_PUMP_WAIT)
               ? FTDI_JTAG_SERVICE_BACKPRESSURE : FTDI_JTAG_SERVICE_IDLE;
}

static FtdiJtagServiceResult ftdi_jtag_handle_events(const FtdiJtagService *const self)
{
    MpssePortEvents events;

    self->config->port->ops->get_events(self->config->port, &events);
    if (events.fault == MPSSE_PORT_FAULT_RX_LENGTH)
    {
        return FTDI_JTAG_SERVICE_PORT_RX_LENGTH_FAULT;
    }
    if (events.fault == MPSSE_PORT_FAULT_RX_OVERWRITE)
    {
        return FTDI_JTAG_SERVICE_PORT_RX_OVERWRITE_FAULT;
    }
    if (events.fault != MPSSE_PORT_FAULT_NONE)
    {
        __builtin_trap();
    }

    /* BL702 对通道 A 的 SIO_RESET、PURGE_RX、PURGE_TX 均执行
     * jtag_mpsse_reset，清 RX/TX、半条命令与 DR32 暂存。
     */
    if ((events.bus_reset != self->state->seen_bus_reset) ||
        (events.sio_reset != self->state->seen_sio_reset) ||
        (events.host_rx_purge != self->state->seen_host_rx_purge) ||
        (events.host_tx_purge != self->state->seen_host_tx_purge))
    {
        JTAGManager_Reset(self->config->jtag);
        JtagTrace_Reset(&JtagTrace0, ((uint32_t)events.bus_reset << 24U) |
                        ((uint32_t)events.sio_reset << 16U) |
                        ((uint32_t)events.host_rx_purge << 8U) | events.host_tx_purge);
        self->state->rx_phase = FTDI_JTAG_RX_IDLE;
        self->config->port->ops->rx_consume(self->config->port);
        self->state->seen_bus_reset = events.bus_reset;
        self->state->seen_sio_reset = events.sio_reset;
        self->state->seen_host_rx_purge = events.host_rx_purge;
        self->state->seen_host_tx_purge = events.host_tx_purge;
        return FTDI_JTAG_SERVICE_PROGRESS;
    }
    return FTDI_JTAG_SERVICE_IDLE;
}

static FtdiJtagPumpResult ftdi_jtag_pump_port_rx(const FtdiJtagService *const self)
{
    const uint8_t *data;
    const uint16_t length = self->config->port->ops->rx_peek(self->config->port, &data);

    if (data == (const uint8_t *)0)
    {
        return FTDI_JTAG_PUMP_IDLE;
    }
    if ((self->state->rx_phase == FTDI_JTAG_RX_COLLECTING) &&
        (JTAGManager_RxFree(self->config->jtag) < MPSSE_PORT_RX_PACKET_SIZE))
    {
        /* BL702 在下一次 OUT 回调、读新包之前检查 offset >4096-64。
         * 因此恰好 4096 字节也要等下一个 OUT（包括 ZLP）才执行满批次。
         */
        self->state->rx_phase = FTDI_JTAG_RX_READY;
        return FTDI_JTAG_PUMP_BATCH_FULL;
    }
    if (length == 0U)
    {
        JtagTrace_Rx(&JtagTrace0, length);
        /* 与 BL702 一样，ZLP 不作为编程页完成标记；没有 2 ms 超时收尾。 */
        self->config->port->ops->rx_consume(self->config->port);
        return FTDI_JTAG_PUMP_IDLE;
    }
    if (JTAGManager_RxWritePacket(self->config->jtag, data, length) != JTAG_RX_PACKET_ACCEPTED)
    {
        /* 容量不足的批次在上面先执行，不能把新包写入仍未执行的区域。 */
        __builtin_trap();
    }
    JtagTrace_Rx(&JtagTrace0, length);
    if (self->state->rx_phase == FTDI_JTAG_RX_IDLE)
    {
        self->state->rx_phase = (ftdi_jtag_has_page_header(data, length) != 0U)
                                   ? FTDI_JTAG_RX_COLLECTING : FTDI_JTAG_RX_READY;
    }
    if (length < MPSSE_PORT_RX_PACKET_SIZE)
    {
        self->state->rx_phase = FTDI_JTAG_RX_READY;
    }
    if (self->state->rx_phase == FTDI_JTAG_RX_COLLECTING)
    {
        self->config->port->ops->rx_consume(self->config->port);
    }
    return FTDI_JTAG_PUMP_PROGRESS;
}

static uint8_t ftdi_jtag_has_page_header(const uint8_t *const data, uint16_t length)
{
    static const uint8_t header[] FTDI_JTAG_SERVICE_FLASH = {
        0x4BU, 0x03U, 0x03U, 0x1BU, 0x06U, 0x71U
    };
    const uint16_t limit = (length < 32U) ? length : 32U;

    for (uint16_t pos = 0U; pos + sizeof(header) <= limit; pos++)
    {
        uint8_t index = 0U;

        while ((index < sizeof(header)) && (data[pos + index] == header[index]))
        {
            index++;
        }
        if (index == sizeof(header))
        {
            return 1U;
        }
    }
    return 0U;
}

static FtdiJtagPumpResult ftdi_jtag_pump_port_tx(const FtdiJtagService *const self)
{
    const uint8_t *head;
    const uint8_t *tail = (const uint8_t *)0;
    uint16_t head_length = JTAGManager_TxPeek(self->config->jtag, 0U, &head);
    uint16_t tail_length = 0U;
    MpssePortResult result;

    if (head_length == 0U)
    {
        return FTDI_JTAG_PUMP_IDLE;
    }
    if (head_length > MPSSE_PORT_TX_PACKET_SIZE)
    {
        head_length = MPSSE_PORT_TX_PACKET_SIZE;
    }
    else if (head_length < MPSSE_PORT_TX_PACKET_SIZE)
    {
        /* BL702 一包读取最多 62 字节，跨环尾也不提前制造 USB 短包。
         * 两段都只借用，USB 成功复制到 PMA 后才一起消费，无第二份 TX 缓存。
         */
        tail_length = JTAGManager_TxPeek(self->config->jtag, head_length, &tail);
        if (tail_length > MPSSE_PORT_TX_PACKET_SIZE - head_length)
        {
            tail_length = (uint16_t)(MPSSE_PORT_TX_PACKET_SIZE - head_length);
        }
    }
    result = self->config->port->ops->tx_write(
        self->config->port, head, head_length, tail, tail_length);
    if (result == MPSSE_PORT_OK)
    {
        JTAGManager_TxConsume(self->config->jtag, (uint16_t)(head_length + tail_length));
        return FTDI_JTAG_PUMP_PROGRESS;
    }
    if ((result == MPSSE_PORT_TX_BUSY) || (result == MPSSE_PORT_NOT_CONFIGURED))
    {
        return FTDI_JTAG_PUMP_WAIT;
    }
    __builtin_trap();
}
