#include "ft232MpssePort.h"

#include "ft232Usbd.h"

#define FT232_MPSSE_PORT_FLASH \
    __attribute__((section(".rodata.ft232_mpsse_port")))

_Static_assert(MPSSE_PORT_RX_PACKET_SIZE == FTDI_USB_BULK_PACKET_SIZE,
               "MPSSE RX boundary must match the USB bulk packet");
_Static_assert(MPSSE_PORT_TX_PACKET_SIZE == FTDI_USB_IN_DATA_SIZE,
               "MPSSE TX boundary must leave room for FTDI status");

static void ft232_mpsse_enable(const MpssePort *self);
static void ft232_mpsse_service(const MpssePort *self);
static uint8_t ft232_mpsse_is_configured(const MpssePort *self);
static void ft232_mpsse_get_events(const MpssePort *self,
                                   MpssePortEvents *events);
static uint16_t ft232_mpsse_rx_peek(const MpssePort *self,
                                    const uint8_t **data);
static void ft232_mpsse_rx_consume(const MpssePort *self);
static MpssePortResult ft232_mpsse_tx_write(const MpssePort *self,
                                            const uint8_t *data,
                                            uint16_t length);
static uint8_t ft232_mpsse_interrupt_lock(const MpssePort *self);
static void ft232_mpsse_interrupt_unlock(const MpssePort *self,
                                         uint8_t token);

static const MpssePortOps ft232_mpsse_ops FT232_MPSSE_PORT_FLASH = {
    .enable = ft232_mpsse_enable,
    .service = ft232_mpsse_service,
    .is_configured = ft232_mpsse_is_configured,
    .get_events = ft232_mpsse_get_events,
    .rx_peek = ft232_mpsse_rx_peek,
    .rx_consume = ft232_mpsse_rx_consume,
    .tx_write = ft232_mpsse_tx_write,
    .interrupt_lock = ft232_mpsse_interrupt_lock,
    .interrupt_unlock = ft232_mpsse_interrupt_unlock
};

const MpssePort MpssePort0 FT232_MPSSE_PORT_FLASH = {
    .ops = &ft232_mpsse_ops,
    .context = &Ft232Usbd0
};

static const Ft232Usbd *ft232_mpsse_usbd(const MpssePort *const self)
{
    return (const Ft232Usbd *)self->context;
}

static void ft232_mpsse_enable(const MpssePort *const self)
{
    Ft232Usbd_DataEnable(ft232_mpsse_usbd(self));
}

static void ft232_mpsse_service(const MpssePort *const self)
{
    Ft232Usbd_MpsseService(ft232_mpsse_usbd(self));
}

static uint8_t ft232_mpsse_is_configured(const MpssePort *const self)
{
    return Ft232Usbd_IsConfigured(ft232_mpsse_usbd(self));
}

static void ft232_mpsse_get_events(const MpssePort *const self,
                                   MpssePortEvents *const events)
{
    Ft232UsbdEvents usb_events;

    Ft232Usbd_GetEvents(ft232_mpsse_usbd(self), &usb_events);
    events->bus_reset = usb_events.bus_reset;
    events->sio_reset = usb_events.sio_reset;
    events->host_rx_purge = usb_events.host_rx_purge;
    events->host_tx_purge = usb_events.host_tx_purge;

    switch (usb_events.fault)
    {
    case FT232_USBD_FAULT_NONE:
        events->fault = MPSSE_PORT_FAULT_NONE;
        break;
    case FT232_USBD_FAULT_OUT_LENGTH:
        events->fault = MPSSE_PORT_FAULT_RX_LENGTH;
        break;
    case FT232_USBD_FAULT_OUT_OVERWRITE:
        events->fault = MPSSE_PORT_FAULT_RX_OVERWRITE;
        break;
    default:
        __builtin_trap();
    }
}

static uint16_t ft232_mpsse_rx_peek(const MpssePort *const self,
                                    const uint8_t **const data)
{
    return Ft232Usbd_MpsseRxPeek(ft232_mpsse_usbd(self), data);
}

static void ft232_mpsse_rx_consume(const MpssePort *const self)
{
    Ft232Usbd_MpsseRxConsume(ft232_mpsse_usbd(self));
}

static MpssePortResult ft232_mpsse_tx_write(const MpssePort *const self,
                                            const uint8_t *const data,
                                            uint16_t length)
{
    const Ft232UsbdResult result =
        Ft232Usbd_MpsseTxWrite(ft232_mpsse_usbd(self), data, length);

    switch (result)
    {
    case FT232_USBD_OK:
        return MPSSE_PORT_OK;
    case FT232_USBD_NOT_CONFIGURED:
        return MPSSE_PORT_NOT_CONFIGURED;
    case FT232_USBD_TX_BUSY:
        return MPSSE_PORT_TX_BUSY;
    case FT232_USBD_INVALID_LENGTH:
        return MPSSE_PORT_INVALID_LENGTH;
    case FT232_USBD_INVALID_DATA:
        return MPSSE_PORT_INVALID_DATA;
    default:
        __builtin_trap();
    }
}

static uint8_t ft232_mpsse_interrupt_lock(const MpssePort *const self)
{
    return Ft232Usbd_InterruptLock(ft232_mpsse_usbd(self));
}

static void ft232_mpsse_interrupt_unlock(const MpssePort *const self,
                                         uint8_t token)
{
    Ft232Usbd_InterruptUnlock(ft232_mpsse_usbd(self), token);
}
