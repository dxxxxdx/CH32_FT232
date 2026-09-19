#include "ftdiJtagService.h"

#define FTDI_JTAG_CLOCK_BUDGET (JTAG_MANAGER_RX_BUFFER_SIZE * 8U)
#define FTDI_JTAG_RX_GAP_MS (2U)

typedef enum
{
    FTDI_JTAG_PUMP_IDLE = 0,
    FTDI_JTAG_PUMP_PROGRESS,
    FTDI_JTAG_PUMP_WAIT
} FtdiJtagPumpResult;

static FtdiJtagServiceResult ftdi_jtag_handle_events(const FtdiJtagService *self);
static FtdiJtagPumpResult ftdi_jtag_pump_port_rx(const FtdiJtagService *self);
static FtdiJtagPumpResult ftdi_jtag_pump_port_tx(const FtdiJtagService *self);
static FtdiJtagServiceResult ftdi_jtag_map_manager_result(JtagServiceResult result);
static uint8_t ftdi_jtag_batch_ready(const FtdiJtagService *self);

FTDI_JTAG_SERVICE_DEFINE(ftdi_jtag_service, MpssePort0,
                         JTAGManager0, FTDI_JTAG_CLOCK_BUDGET);

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
    }

    switch (result)
    {
    case FTDI_JTAG_SERVICE_IDLE:
    case FTDI_JTAG_SERVICE_PROGRESS:
    case FTDI_JTAG_SERVICE_WAIT_CONFIGURATION:
    case FTDI_JTAG_SERVICE_BACKPRESSURE:
    case FTDI_JTAG_SERVICE_MPSSE_FAULT:
        /* 外部 MPSSE/transfer 错误保持明确状态，等待主机 reset 重新同步。 */
        break;
    case FTDI_JTAG_SERVICE_PORT_RX_LENGTH_FAULT:
    case FTDI_JTAG_SERVICE_PORT_RX_OVERWRITE_FAULT:
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
    self->state->rx_last_packet_at = 0U;
    self->state->rx_batch_ready = 0U;
    self->config->port->ops->enable(self->config->port);
}

FtdiJtagServiceResult FtdiJtagService_Service(const FtdiJtagService *const self)
{
    FtdiJtagServiceResult result;
    FtdiJtagPumpResult pump_result;
    JtagServiceResult manager_result;
    uint8_t irq_token;
    uint8_t progressed = 0U;
    uint8_t waiting = 0U;

    result = ftdi_jtag_handle_events(self);
    if (result != FTDI_JTAG_SERVICE_IDLE)
    {
        return result;
    }
    if (self->config->port->ops->is_configured(self->config->port) == 0U)
    {
        return FTDI_JTAG_SERVICE_WAIT_CONFIGURATION;
    }

    /* 先发送上一轮已经生成的回复。 */
    pump_result = ftdi_jtag_pump_port_tx(self);
    if (pump_result == FTDI_JTAG_PUMP_PROGRESS)
    {
        progressed = 1U;
    }
    else if (pump_result == FTDI_JTAG_PUMP_WAIT)
    {
        waiting = 1U;
    }
    pump_result = ftdi_jtag_pump_port_rx(self);
    if (pump_result == FTDI_JTAG_PUMP_PROGRESS)
    {
        progressed = 1U;
    }
    else if (pump_result == FTDI_JTAG_PUMP_WAIT)
    {
        waiting = 1U;
    }

    if (ftdi_jtag_batch_ready(self) != 0U)
    {
        /* 先收批次再连续执行，执行期间不搬入下一批。保存全局IRQ状态，
         * 防止USB和UART中断插入页内；只有TX背压允许提前让出以发送回复。
         * 锁内重新检查reset/purge，不能执行收包时已经取消的字节。
         */
        irq_token = self->config->port->ops->interrupt_lock(self->config->port);
        result = ftdi_jtag_handle_events(self);
        if (result != FTDI_JTAG_SERVICE_IDLE)
        {
            self->config->port->ops->interrupt_unlock(self->config->port,
                                                       irq_token);
            return result;
        }

        manager_result = JTAGManager_Service(self->config->jtag,
                                             self->config->clock_budget);
        self->config->port->ops->interrupt_unlock(self->config->port, irq_token);
        if (JTAGManager_RxUsed(self->config->jtag) == 0U)
        {
            self->state->rx_batch_ready = 0U;
        }
        result = ftdi_jtag_map_manager_result(manager_result);
        if (result == FTDI_JTAG_SERVICE_MPSSE_FAULT)
        {
            return result;
        }
        if (result == FTDI_JTAG_SERVICE_PROGRESS)
        {
            progressed = 1U;
        }
    }

    /* OUT 和 Manager 都推进后再次检查真实回复。只有 31 60 的兼容状态包
     * 由 USB 层在 latency 到期后管理；JTAG 层不能把它混入
     * 解析器 TX 队列。
     */
    pump_result = ftdi_jtag_pump_port_tx(self);
    if (pump_result == FTDI_JTAG_PUMP_PROGRESS)
    {
        progressed = 1U;
    }
    else if (pump_result == FTDI_JTAG_PUMP_WAIT)
    {
        waiting = 1U;
    }
    pump_result = ftdi_jtag_pump_port_rx(self);
    if (pump_result == FTDI_JTAG_PUMP_PROGRESS)
    {
        progressed = 1U;
    }
    else if (pump_result == FTDI_JTAG_PUMP_WAIT)
    {
        waiting = 1U;
    }

    /* 真实 TX 已经获得两次优先发送机会，最后才允许 USB 层补空闲状态包。
     * JTAG 核只调用 port service，不读取 USB 时钟、端点或 bit mode。
     */
    self->config->port->ops->service(self->config->port);

    if (progressed != 0U)
    {
        return FTDI_JTAG_SERVICE_PROGRESS;
    }
    return (waiting != 0U) ? FTDI_JTAG_SERVICE_BACKPRESSURE
                           : FTDI_JTAG_SERVICE_IDLE;
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

    if ((events.bus_reset != self->state->seen_bus_reset) ||
        (events.sio_reset != self->state->seen_sio_reset))
    {
        JTAGManager_Reset(self->config->jtag);
        self->state->rx_batch_ready = 0U;
        self->config->port->ops->rx_consume(self->config->port);
        self->state->seen_bus_reset = events.bus_reset;
        self->state->seen_sio_reset = events.sio_reset;
        self->state->seen_host_rx_purge = events.host_rx_purge;
        self->state->seen_host_tx_purge = events.host_tx_purge;
        return FTDI_JTAG_SERVICE_PROGRESS;
    }

    if (events.host_rx_purge != self->state->seen_host_rx_purge)
    {
        /* 主机 RX 对应设备 TX。 */
        JTAGManager_TxPurge(self->config->jtag);
        self->state->seen_host_rx_purge = events.host_rx_purge;
    }
    if (events.host_tx_purge != self->state->seen_host_tx_purge)
    {
        /* 主机 TX 对应设备 RX，连同半条 MPSSE 命令一起清除。 */
        JTAGManager_RxPurge(self->config->jtag);
        self->state->rx_batch_ready = 0U;
        self->config->port->ops->rx_consume(self->config->port);
        self->state->seen_host_tx_purge = events.host_tx_purge;
    }
    return FTDI_JTAG_SERVICE_IDLE;
}

static FtdiJtagPumpResult ftdi_jtag_pump_port_rx(const FtdiJtagService *const self)
{
    const uint8_t *data;
    uint16_t length;
    JtagRxPacketResult rx_result;

    if (self->state->rx_batch_ready != 0U)
    {
        return FTDI_JTAG_PUMP_IDLE;
    }
    length = self->config->port->ops->rx_peek(self->config->port, &data);
    if (length == 0U)
    {
        return FTDI_JTAG_PUMP_IDLE;
    }
    rx_result = JTAGManager_RxWritePacket(
        self->config->jtag, data, length);
    if (rx_result == JTAG_RX_PACKET_BACKPRESSURE)
    {
        return FTDI_JTAG_PUMP_WAIT;
    }
    if (rx_result == JTAG_RX_PACKET_ACCEPTED)
    {
        self->state->rx_last_packet_at =
            self->config->port->ops->time_now(self->config->port);
        if ((length < MPSSE_PORT_RX_PACKET_SIZE) ||
            (JTAGManager_RxFree(self->config->jtag) < MPSSE_PORT_RX_PACKET_SIZE))
        {
            self->state->rx_batch_ready = 1U;
        }
        self->config->port->ops->rx_consume(self->config->port);
        return FTDI_JTAG_PUMP_PROGRESS;
    }
    /* EP2 已保证 1..64 字节且邮箱数据有效，命中表示内部边界契约损坏。 */
    __builtin_trap();
}

static uint8_t ftdi_jtag_batch_ready(const FtdiJtagService *const self)
{
    if (JTAGManager_RxUsed(self->config->jtag) == 0U)
    {
        return 0U;
    }
    if ((self->state->rx_batch_ready == 0U) &&
        (self->config->port->ops->time_elapsed(self->config->port,
            self->state->rx_last_packet_at, FTDI_JTAG_RX_GAP_MS) != 0U))
    {
        /* 主机可能在64B整数倍处等待读回复，不保证发送短包或ZLP。 */
        self->state->rx_batch_ready = 1U;
    }
    return self->state->rx_batch_ready;
}

static FtdiJtagPumpResult ftdi_jtag_pump_port_tx(const FtdiJtagService *const self)
{
    const uint8_t *payload;
    const uint16_t available = JTAGManager_TxPeek(self->config->jtag, &payload);
    uint16_t payload_length;
    MpssePortResult port_result;

    if (available == 0U)
    {
        return FTDI_JTAG_PUMP_IDLE;
    }

    payload_length = (available < MPSSE_PORT_TX_PACKET_SIZE)
                         ? available : MPSSE_PORT_TX_PACKET_SIZE;
    port_result = self->config->port->ops->tx_write(
        self->config->port, payload, payload_length);
    if (port_result == MPSSE_PORT_OK)
    {
        if (payload_length != 0U)
        {
            JTAGManager_TxConsume(self->config->jtag, payload_length);
        }
        return FTDI_JTAG_PUMP_PROGRESS;
    }
    if ((port_result == MPSSE_PORT_TX_BUSY) ||
        (port_result == MPSSE_PORT_NOT_CONFIGURED))
    {
        return FTDI_JTAG_PUMP_WAIT;
    }

    /* payload 来自内部 RB 且固定限制为 1..62 字节，其余结果是内部契约破坏。 */
    __builtin_trap();
}

static FtdiJtagServiceResult ftdi_jtag_map_manager_result(JtagServiceResult result)
{
    switch (result)
    {
    case JTAG_SERVICE_IDLE:
    case JTAG_SERVICE_WAIT_RX:
    case JTAG_SERVICE_WAIT_TX:
        return FTDI_JTAG_SERVICE_IDLE;
    case JTAG_SERVICE_BUDGET_REACHED:
        return FTDI_JTAG_SERVICE_PROGRESS;
    case JTAG_SERVICE_INVALID_ARGUMENT:
        return FTDI_JTAG_SERVICE_MPSSE_FAULT;
    default:
        __builtin_trap();
    }
}
