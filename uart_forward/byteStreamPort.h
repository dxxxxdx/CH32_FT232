#ifndef CH32_FT232_BYTE_STREAM_PORT_H
#define CH32_FT232_BYTE_STREAM_PORT_H

#include <stdint.h>

typedef struct ByteStreamPort ByteStreamPort;

typedef enum
{
    BYTE_STREAM_PORT_OK = 0,
    BYTE_STREAM_PORT_NOT_READY,
    BYTE_STREAM_PORT_TX_BUSY,
    BYTE_STREAM_PORT_INVALID_LENGTH,
    BYTE_STREAM_PORT_IO_FAULT
} ByteStreamPortResult;

typedef enum
{
    BYTE_STREAM_PORT_FAULT_NONE = 0,
    BYTE_STREAM_PORT_FAULT_RX_LENGTH,
    BYTE_STREAM_PORT_FAULT_RX_OVERWRITE,
    BYTE_STREAM_PORT_FAULT_RX_OVERFLOW,
    BYTE_STREAM_PORT_FAULT_RX_DMA,
    BYTE_STREAM_PORT_FAULT_TX_DMA,
    BYTE_STREAM_PORT_FAULT_UART_PARITY,
    BYTE_STREAM_PORT_FAULT_UART_FRAMING,
    BYTE_STREAM_PORT_FAULT_UART_NOISE,
    BYTE_STREAM_PORT_FAULT_UART_OVERRUN
} ByteStreamPortFault;

typedef struct
{
    void (*enable)(const ByteStreamPort *self);
    void (*service)(const ByteStreamPort *self);
    uint8_t (*is_ready)(const ByteStreamPort *self);
    ByteStreamPortFault (*fault)(const ByteStreamPort *self);
    uint16_t (*rx_peek)(const ByteStreamPort *self, const uint8_t **data);
    void (*rx_consume)(const ByteStreamPort *self, uint16_t length);
    ByteStreamPortResult (*tx_write)(const ByteStreamPort *self,
                                     const uint8_t *data,
                                     uint16_t length);
} ByteStreamPortOps;

struct ByteStreamPort
{
    const ByteStreamPortOps *const ops;
    const void *const context;
};

#endif /* CH32_FT232_BYTE_STREAM_PORT_H */
