#ifndef CH32_FT232_MPSSE_PORT_H
#define CH32_FT232_MPSSE_PORT_H

#include <stdint.h>

#define MPSSE_PORT_RX_PACKET_SIZE (64U)
#define MPSSE_PORT_TX_PACKET_SIZE (62U)

typedef enum
{
    MPSSE_PORT_FAULT_NONE = 0,
    MPSSE_PORT_FAULT_RX_LENGTH,
    MPSSE_PORT_FAULT_RX_OVERWRITE
} MpssePortFault;

typedef struct
{
    uint8_t bus_reset;
    uint8_t sio_reset;
    uint8_t host_rx_purge;
    uint8_t host_tx_purge;
    uint8_t fault;
} MpssePortEvents;

typedef enum
{
    MPSSE_PORT_OK = 0,
    MPSSE_PORT_NOT_CONFIGURED,
    MPSSE_PORT_TX_BUSY,
    MPSSE_PORT_INVALID_LENGTH,
    MPSSE_PORT_INVALID_DATA
} MpssePortResult;

typedef struct MpssePort MpssePort;

typedef struct
{
    void (*enable)(const MpssePort *self);
    /* 推进 FTDI/USB 自身的传输语义，不得在这里解析或生成 MPSSE 回复。 */
    void (*service)(const MpssePort *self);
    uint8_t (*is_configured)(const MpssePort *self);
    void (*get_events)(const MpssePort *self, MpssePortEvents *events);
    uint16_t (*rx_peek)(const MpssePort *self, const uint8_t **data);
    void (*rx_consume)(const MpssePort *self);
    MpssePortResult (*tx_write)(const MpssePort *self,
                                const uint8_t *data,
                                uint16_t length);
    uint8_t (*interrupt_lock)(const MpssePort *self);
    void (*interrupt_unlock)(const MpssePort *self, uint8_t token);
} MpssePortOps;

struct MpssePort
{
    const MpssePortOps *const ops;
    const void *const context;
};

/* 由 USB/BSP 层提供的编译期默认数据端口。 */
extern const MpssePort MpssePort0;

#endif /* CH32_FT232_MPSSE_PORT_H */
