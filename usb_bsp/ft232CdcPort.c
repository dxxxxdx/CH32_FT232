#include "ft232CdcPort.h"

#include "ft232Usbd.h"

#define FT232_CDC_PORT_FLASH __attribute__((section(".rodata.ft232_cdc_port")))

static void ft232_cdc_port_enable(const ByteStreamPort *self);
static void ft232_cdc_port_service(const ByteStreamPort *self);
static uint8_t ft232_cdc_port_is_ready(const ByteStreamPort *self);
static ByteStreamPortFault ft232_cdc_port_fault(const ByteStreamPort *self);
static uint16_t ft232_cdc_port_rx_peek(const ByteStreamPort *self,
                                       const uint8_t **data);
static void ft232_cdc_port_rx_consume(const ByteStreamPort *self,
                                      uint16_t length);
static ByteStreamPortResult ft232_cdc_port_tx_write(const ByteStreamPort *self,
                                                    const uint8_t *data,
                                                    uint16_t length);

static const ByteStreamPortOps ft232_cdc_port_ops FT232_CDC_PORT_FLASH = {
    .enable = ft232_cdc_port_enable,
    .service = ft232_cdc_port_service,
    .is_ready = ft232_cdc_port_is_ready,
    .fault = ft232_cdc_port_fault,
    .rx_peek = ft232_cdc_port_rx_peek,
    .rx_consume = ft232_cdc_port_rx_consume,
    .tx_write = ft232_cdc_port_tx_write
};

const ByteStreamPort UsbCdcPort0 FT232_CDC_PORT_FLASH = {
    .ops = &ft232_cdc_port_ops,
    .context = &Ft232Usbd0
};

static const Ft232Usbd *ft232_cdc_port_usbd(const ByteStreamPort *const self)
{
    return (const Ft232Usbd *)self->context;
}

static void ft232_cdc_port_enable(const ByteStreamPort *const self)
{
    Ft232Usbd_CdcDataEnable(ft232_cdc_port_usbd(self));
}

static void ft232_cdc_port_service(const ByteStreamPort *const self)
{
    (void)self;
}

static uint8_t ft232_cdc_port_is_ready(const ByteStreamPort *const self)
{
    return Ft232Usbd_CdcIsReady(ft232_cdc_port_usbd(self));
}

static ByteStreamPortFault ft232_cdc_port_fault(const ByteStreamPort *const self)
{
    switch (Ft232Usbd_CdcGetFault(ft232_cdc_port_usbd(self)))
    {
    case FT232_USBD_CDC_FAULT_NONE:
        return BYTE_STREAM_PORT_FAULT_NONE;
    case FT232_USBD_CDC_FAULT_OUT_LENGTH:
        return BYTE_STREAM_PORT_FAULT_RX_LENGTH;
    case FT232_USBD_CDC_FAULT_OUT_OVERWRITE:
        return BYTE_STREAM_PORT_FAULT_RX_OVERWRITE;
    default:
        __builtin_trap();
    }
}

static uint16_t ft232_cdc_port_rx_peek(const ByteStreamPort *const self,
                                       const uint8_t **const data)
{
    return Ft232Usbd_CdcRxPeek(ft232_cdc_port_usbd(self), data);
}

static void ft232_cdc_port_rx_consume(const ByteStreamPort *const self,
                                      uint16_t length)
{
    /* EP6 的单包上限与转发块相同，消费单位永远是一整个 USB 邮箱。 */
    (void)length;
    Ft232Usbd_CdcRxConsume(ft232_cdc_port_usbd(self));
}

static ByteStreamPortResult ft232_cdc_port_tx_write(
    const ByteStreamPort *const self,
    const uint8_t *const data,
    uint16_t length)
{
    switch (Ft232Usbd_CdcTxWrite(ft232_cdc_port_usbd(self), data, length))
    {
    case FT232_USBD_OK:
        return BYTE_STREAM_PORT_OK;
    case FT232_USBD_NOT_CONFIGURED:
        return BYTE_STREAM_PORT_NOT_READY;
    case FT232_USBD_TX_BUSY:
        return BYTE_STREAM_PORT_TX_BUSY;
    case FT232_USBD_INVALID_LENGTH:
        return BYTE_STREAM_PORT_INVALID_LENGTH;
    case FT232_USBD_INVALID_DATA:
        __builtin_trap();
    default:
        __builtin_trap();
    }
}

_Static_assert(FTDI_USB_CDC_DATA_PACKET_SIZE == 16U,
               "CDC stream adapter expects 16-byte USB packets");
