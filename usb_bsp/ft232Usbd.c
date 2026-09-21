#include "ft232Usbd.h"

#include "boardtype/BoardGpio.h"
#include "debug.h"
#include "ft232Descriptor.h"
#include "usb_lib.h"
#include "SystemTimebase.h"

#define USB_REQUEST_DIRECTION_IN       (0x80U)
#define USB_VENDOR_HOST_TO_DEVICE      (0x40U)
#define USB_VENDOR_DEVICE_TO_HOST      (0xC0U)
#if USB_CDC_ENABLED != 0U
#define USB_CLASS_INTERFACE_OUT        (0x21U)
#define USB_CLASS_INTERFACE_IN         (0xA1U)
#endif
#define USB_LANGUAGE_ID_EN_US_LOW      (0x09U)
#define USB_LANGUAGE_ID_EN_US_HIGH     (0x04U)
#define FTDI_DEFAULT_LATENCY_TIMER      (16U)
#define FT232_USBD_JTAG_CHANNEL         (0U)
#define FT232_USBD_AUX_CHANNEL          (1U)

#define USBD_ENDPOINT_RESET_MASK       (0x7F7FU)
#define USBD_EP0_RX_BLOCK_MASK         (0xFC00U)
#define USBD_PMA_SIZE                  (0x0200U)
#define FT232_USBD_MPSSE_IDLE_STATUS_DELAY_MS (1U)
#define FT232_USBD_FLASH __attribute__((section(".rodata.usbd")))

static const Ft232UsbdConfig ft232_usbd_config FT232_USBD_FLASH = {
    .usb_clock_source = BOARD_USB_CLOCK_SOURCE,
    .interrupt = {
        .NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn,
        .NVIC_IRQChannelPreemptionPriority = 1U,
        .NVIC_IRQChannelSubPriority = 0U,
        .NVIC_IRQChannelCmd = ENABLE
    },
    .gpio = &BoardGpio0
};

static Ft232UsbdState ft232_usbd_state;

const Ft232Usbd Ft232Usbd0 FT232_USBD_FLASH = {
    .config = &ft232_usbd_config,
    .state = &ft232_usbd_state
};

static void ft232_usbd_device_init(void);
static void ft232_usbd_reset(void);
static void ft232_usbd_status_in(void);
static void ft232_usbd_status_out(void);
static RESULT ft232_usbd_data_setup(uint8_t request);
static RESULT ft232_usbd_no_data_setup(uint8_t request);
static RESULT ft232_usbd_ftdi_data_setup(uint8_t request);
static RESULT ft232_usbd_ftdi_no_data_setup(uint8_t request);
#if USB_CDC_ENABLED != 0U
static RESULT ft232_usbd_cdc_data_setup(uint8_t request);
static RESULT ft232_usbd_cdc_no_data_setup(uint8_t request);
static void ft232_usbd_cdc_fixed_line_coding(Ft232UsbdState *state);
#endif
static RESULT ft232_usbd_get_interface_setting(uint8_t interface,
                                                uint8_t alternate_setting);
static uint8_t *ft232_usbd_get_control_reply(uint16_t length);
static uint8_t *ft232_usbd_get_device_descriptor(uint16_t length);
static uint8_t *ft232_usbd_get_config_descriptor(uint16_t length);
static uint8_t *ft232_usbd_get_string_descriptor(uint16_t length);
static uint8_t *ft232_usbd_get_bos_descriptor(uint16_t length);
static uint8_t *ft232_usbd_get_ms_os_20_descriptor(uint16_t length);
static void ft232_usbd_set_configuration(void);
static void ft232_usbd_set_address(void);
static void ft232_usbd_ep1_in(void);
static void ft232_usbd_ep2_out(void);
static void ft232_usbd_ep3_in(void);
static void ft232_usbd_ep4_out(void);
#if USB_CDC_ENABLED != 0U
static void ft232_usbd_ep6_out(void);
static void ft232_usbd_ep7_in(void);
#endif
static void ft232_usbd_port_set(uint8_t connected);
static void ft232_usbd_interrupt_service(void);
static Ft232UsbdChannelState *ft232_usbd_request_channel(Ft232UsbdState *state,
                                                         uint8_t allow_index_high);
static uint16_t ft232_usbd_rx_peek(Ft232UsbdChannelState *channel,
                                   const uint8_t **data);
static void ft232_usbd_rx_consume(const Ft232Usbd *self,
                                  Ft232UsbdChannelState *channel,
                                  uint8_t endpoint);
static Ft232UsbdResult ft232_usbd_tx_write(const Ft232Usbd *self,
                                           Ft232UsbdChannelState *channel,
                                           uint8_t endpoint_address,
                                           uint8_t endpoint,
                                           const uint8_t *data,
                                           uint16_t length);
static void ft232_usbd_in_complete(Ft232UsbdChannelState *channel,
                                   uint8_t endpoint);
static void ft232_usbd_out_receive(Ft232UsbdState *state,
                                   Ft232UsbdChannelState *channel,
                                   uint8_t endpoint_address,
                                   uint8_t endpoint);
static void ft232_usbd_cancel_data(Ft232UsbdState *state);
static void ft232_usbd_cancel_channel(Ft232UsbdChannelState *channel);
static void ft232_usbd_purge_device_in(Ft232UsbdChannelState *channel,
                                       uint8_t endpoint);
#if USB_CDC_ENABLED != 0U
static void ft232_usbd_cancel_cdc(Ft232UsbdCdcState *cdc);
#endif
static uint32_t ft232_usbd_systick_low(void);

static const ONE_DESCRIPTOR ft232_device_descriptor FT232_USBD_FLASH = {
    (uint8_t *)FtdiUsbDeviceDescriptor,
    FTDI_USB_DEVICE_DESC_SIZE
};

static const ONE_DESCRIPTOR ft232_config_descriptor FT232_USBD_FLASH = {
    (uint8_t *)FtdiUsbConfigurationDescriptor,
    FTDI_USB_CONFIG_DESC_SIZE
};

static const ONE_DESCRIPTOR ft232_string_descriptors[] FT232_USBD_FLASH = {
    {(uint8_t *)FtdiUsbLanguageDescriptor, FTDI_USB_LANG_DESC_SIZE},
    {(uint8_t *)FtdiUsbManufacturerDescriptor, FTDI_USB_MANUFACTURER_DESC_SIZE},
    {(uint8_t *)FtdiUsbProductDescriptor, FTDI_USB_PRODUCT_DESC_SIZE},
    {(uint8_t *)FtdiUsbSerialDescriptor, FTDI_USB_SERIAL_DESC_SIZE}
};

static const ONE_DESCRIPTOR ft232_bos_descriptor FT232_USBD_FLASH = {
    (uint8_t *)FtdiUsbBosDescriptor,
    FTDI_USB_BOS_DESC_SIZE
};

static const ONE_DESCRIPTOR ft232_ms_os_20_descriptor FT232_USBD_FLASH = {
    (uint8_t *)FtdiUsbMsOs20Descriptor,
    FTDI_USB_MS_OS_20_DESC_SIZE
};

/* Gowin/FTDI 工具会在重新打开通道时探测 EEPROM。字符串镜像与 USB
 * 描述符保持一致，避免 D2XX 扫描和二次打开得到两个不同的产品名。
 * word 7/8 的低字节是字符串偏移加 0x80，高字节是 USB 字符串描述符长度。
 * 末 word 0xD457 按 FTDI 0xAAAA 初值、逐 word 异或后左循环一位计算。
 */
static const uint16_t ft232_eeprom_words[] FT232_USBD_FLASH = {
    0x0800U, 0x0403U, 0x6010U, 0x0500U, 0x3280U, 0x0000U, 0x0200U, 0x0A96U,
    0x1CA0U, 0x0000U, 0x0046U, 0x030AU, 0x0043U, 0x0048U, 0x0033U, 0x0032U,
    0x031CU, 0x0044U, 0x0075U, 0x0061U, 0x006CU, 0x0020U, 0x0052U, 0x0053U,
    0x0032U, 0x0033U, 0x0032U, 0x002DU, 0x0048U, 0x0053U, 0x0000U, 0x0000U,
    0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
    0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
    0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
    0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0xD457U
};

DEVICE Device_Table = {
    EP_NUM,
    1U
};

DEVICE_PROP Device_Property = {
    ft232_usbd_device_init,
    ft232_usbd_reset,
    ft232_usbd_status_in,
    ft232_usbd_status_out,
    ft232_usbd_data_setup,
    ft232_usbd_no_data_setup,
    ft232_usbd_get_interface_setting,
    ft232_usbd_get_device_descriptor,
    ft232_usbd_get_config_descriptor,
    ft232_usbd_get_string_descriptor,
    NULL,
    FTDI_USB_EP0_PACKET_SIZE
};

USER_STANDARD_REQUESTS User_Standard_Requests = {
    NOP_Process,
    ft232_usbd_set_configuration,
    NOP_Process,
    NOP_Process,
    NOP_Process,
    NOP_Process,
    NOP_Process,
    NOP_Process,
    ft232_usbd_set_address
};

uint16_t Ep0RxBlks;
__IO uint16_t wIstr;

/* 数组必须覆盖库可分发的所有端点，未发布的端点仍明确落到空处理。 */
void (*pEpInt_IN[7])(void) = {
    ft232_usbd_ep1_in, NOP_Process, ft232_usbd_ep3_in, NOP_Process,
#if USB_CDC_ENABLED != 0U
    NOP_Process, NOP_Process, ft232_usbd_ep7_in
#else
    NOP_Process, NOP_Process, NOP_Process
#endif
};

void (*pEpInt_OUT[7])(void) = {
    NOP_Process, ft232_usbd_ep2_out, NOP_Process, ft232_usbd_ep4_out,
#if USB_CDC_ENABLED != 0U
    NOP_Process, ft232_usbd_ep6_out, NOP_Process
#else
    NOP_Process, NOP_Process, NOP_Process
#endif
};

void Ft232Usbd_Init(const Ft232Usbd *const self)
{
    Ft232UsbdState *const state = self->state;

    for (uint8_t channel = 0U; channel < FTDI_USB_INTERFACE_COUNT; channel++)
    {
        state->channel[channel].out_length = 0U;
        state->channel[channel].out_produced = 0U;
        state->channel[channel].out_consumed = 0U;
        state->channel[channel].in_produced = 0U;
        state->channel[channel].in_consumed = 0U;
        state->channel[channel].latency_timer = FTDI_DEFAULT_LATENCY_TIMER;
        state->channel[channel].bit_mode = FTDI_SIO_BITMODE_RESET;
    }
    state->control_reply_length = 0U;
    state->configured = 0U;
    state->data_enabled = 0U;
    state->bus_reset_event = 0U;
    state->sio_reset_event = 0U;
    state->host_rx_purge_event = 0U;
    state->host_tx_purge_event = 0U;
    state->fault = FT232_USBD_FAULT_NONE;
#if USB_CDC_ENABLED != 0U
    state->cdc.out_length = 0U;
    state->cdc.out_produced = 0U;
    state->cdc.out_consumed = 0U;
    state->cdc.in_produced = 0U;
    state->cdc.in_consumed = 0U;
    state->cdc.data_enabled = 0U;
    state->cdc.fault = FT232_USBD_CDC_FAULT_NONE;
    state->cdc.control_line_state = 0U;
#endif
    state->mpsse_last_data_at = 0U;
    state->mpsse_idle_elapsed = 0U;

    RCC_USBCLKConfig(self->config->usb_clock_source);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);

    USB_Init();
}

void Ft232Usbd_InterruptInit(const Ft232Usbd *const self)
{
    NVIC_Init((NVIC_InitTypeDef *)&self->config->interrupt);
}

void Ft232Usbd_DataEnable(const Ft232Usbd *const self)
{
    Ft232UsbdState *const state = self->state;
    const uint8_t irq_was_enabled = Ft232Usbd_InterruptLock(self);

    state->data_enabled = 1U;
    if ((state->configured != 0U) &&
        (state->channel[FT232_USBD_JTAG_CHANNEL].out_produced ==
         state->channel[FT232_USBD_JTAG_CHANNEL].out_consumed))
    {
        SetEPRxCount(ENDP2, FTDI_USB_BULK_PACKET_SIZE);
        SetEPRxValid(ENDP2);
    }
    if ((state->configured != 0U) &&
        (state->channel[FT232_USBD_AUX_CHANNEL].out_produced ==
         state->channel[FT232_USBD_AUX_CHANNEL].out_consumed))
    {
        SetEPRxCount(ENDP4, FTDI_USB_BULK_PACKET_SIZE);
        SetEPRxValid(ENDP4);
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
}

void Ft232Usbd_MpsseService(const Ft232Usbd *const self)
{
    Ft232UsbdState *const state = self->state;
    Ft232UsbdChannelState *const channel = &state->channel[FT232_USBD_JTAG_CHANNEL];
    const uint8_t irq_was_enabled = Ft232Usbd_InterruptLock(self);

    /* 调用者只在未收集/执行 OUT 且真实 TX 为空时调用。BL702 不按 SET_LATENCY
     * 或 bit mode 门控 JTAG 空包；真实数据后超过 1 ms 即可回 31 60。
     * elapsed 一旦成立便保持，避免 32 位 HCLK 计数回绕后短暂重新等待。
     */
    if ((state->mpsse_idle_elapsed == 0U) &&
        ((uint32_t)(ft232_usbd_systick_low() - state->mpsse_last_data_at) >
         (SystemCoreClock / 1000U) * FT232_USBD_MPSSE_IDLE_STATUS_DELAY_MS))
    {
        state->mpsse_idle_elapsed = 1U;
    }
    if ((state->mpsse_idle_elapsed != 0U) &&
        (state->configured != 0U) && (state->data_enabled != 0U) &&
        (channel->out_produced == channel->out_consumed) &&
        (channel->in_produced == channel->in_consumed))
    {
        state->mpsse_in_packet[0] = FTDI_USB_MODEM_STATUS;
        state->mpsse_in_packet[1] = FTDI_USB_LINE_STATUS;
        (void)USB_SIL_Write(FTDI_USB_JTAG_IN_EP, state->mpsse_in_packet,
                            FTDI_USB_STATUS_SIZE);
        __asm volatile ("" ::: "memory");
        channel->in_produced++;
        SetEPTxValid(ENDP1);
        /* BL702 空包不刷新 last_send，取走后下一轮仍可再发。 */
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
}

uint8_t Ft232Usbd_IsConfigured(const Ft232Usbd *const self)
{
    return self->state->configured;
}

void Ft232Usbd_GetEvents(const Ft232Usbd *const self,
                         Ft232UsbdEvents *const events)
{
    events->bus_reset = self->state->bus_reset_event;
    events->sio_reset = self->state->sio_reset_event;
    events->host_rx_purge = self->state->host_rx_purge_event;
    events->host_tx_purge = self->state->host_tx_purge_event;
    events->fault = self->state->fault;
}

uint16_t Ft232Usbd_MpsseRxPeek(const Ft232Usbd *const self,
                               const uint8_t **const data)
{
    return ft232_usbd_rx_peek(&self->state->channel[FT232_USBD_JTAG_CHANNEL],
                              data);
}

void Ft232Usbd_MpsseRxConsume(const Ft232Usbd *const self)
{
    ft232_usbd_rx_consume(self,
                          &self->state->channel[FT232_USBD_JTAG_CHANNEL],
                          ENDP2);
}

Ft232UsbdResult Ft232Usbd_MpsseTxWrite(const Ft232Usbd *const self,
                                       const uint8_t *const head, uint16_t head_length,
                                       const uint8_t *const tail, uint16_t tail_length)
{
    const uint32_t length = (uint32_t)head_length + tail_length;

    if ((head_length == 0U) || (length > FTDI_USB_IN_DATA_SIZE))
    {
        return FT232_USBD_INVALID_LENGTH;
    }
    if ((head == (const uint8_t *)0) ||
        ((tail_length != 0U) && (tail == (const uint8_t *)0)))
    {
        return FT232_USBD_INVALID_DATA;
    }

    /* 在原有 USB 静态包中拼接环尾两段，不增加 service 影子缓冲。 */
    self->state->mpsse_in_packet[0] = FTDI_USB_MODEM_STATUS;
    self->state->mpsse_in_packet[1] = FTDI_USB_LINE_STATUS;
    for (uint16_t index = 0U; index < head_length; index++)
    {
        self->state->mpsse_in_packet[FTDI_USB_STATUS_SIZE + index] = head[index];
    }
    for (uint16_t index = 0U; index < tail_length; index++)
    {
        self->state->mpsse_in_packet[FTDI_USB_STATUS_SIZE + head_length + index] = tail[index];
    }
    return ft232_usbd_tx_write(self,
                               &self->state->channel[FT232_USBD_JTAG_CHANNEL],
                               FTDI_USB_JTAG_IN_EP, ENDP1, self->state->mpsse_in_packet,
                               (uint16_t)(FTDI_USB_STATUS_SIZE + length));
}

uint16_t Ft232Usbd_AuxRxPeek(const Ft232Usbd *const self,
                             const uint8_t **const data)
{
    return ft232_usbd_rx_peek(&self->state->channel[FT232_USBD_AUX_CHANNEL],
                              data);
}

void Ft232Usbd_AuxRxConsume(const Ft232Usbd *const self)
{
    ft232_usbd_rx_consume(self,
                          &self->state->channel[FT232_USBD_AUX_CHANNEL],
                          ENDP4);
}

Ft232UsbdResult Ft232Usbd_AuxTxWrite(const Ft232Usbd *const self,
                                     const uint8_t *const data,
                                     uint16_t length)
{
    return ft232_usbd_tx_write(self,
                               &self->state->channel[FT232_USBD_AUX_CHANNEL],
                               FTDI_USB_AUX_IN_EP, ENDP3, data, length);
}

#if USB_CDC_ENABLED != 0U
void Ft232Usbd_CdcDataEnable(const Ft232Usbd *const self)
{
    Ft232UsbdCdcState *const cdc = &self->state->cdc;
    const uint8_t irq_was_enabled = Ft232Usbd_InterruptLock(self);

    cdc->data_enabled = 1U;
    if ((self->state->configured != 0U) &&
        (cdc->out_produced == cdc->out_consumed))
    {
        SetEPRxCount(ENDP6, FTDI_USB_CDC_DATA_PACKET_SIZE);
        SetEPRxValid(ENDP6);
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
}

uint8_t Ft232Usbd_CdcIsReady(const Ft232Usbd *const self)
{
    return ((self->state->configured != 0U) &&
            (self->state->cdc.data_enabled != 0U)) ? 1U : 0U;
}

uint8_t Ft232Usbd_CdcIsOpen(const Ft232Usbd *const self)
{
    return (Ft232Usbd_CdcIsReady(self) != 0U &&
            (self->state->cdc.control_line_state & 1U) != 0U) ? 1U : 0U;
}

Ft232UsbdCdcFault Ft232Usbd_CdcGetFault(const Ft232Usbd *const self)
{
    return (Ft232UsbdCdcFault)self->state->cdc.fault;
}

uint16_t Ft232Usbd_CdcRxPeek(const Ft232Usbd *const self,
                             const uint8_t **const data)
{
    const Ft232UsbdCdcState *const cdc = &self->state->cdc;

    if (cdc->out_produced == cdc->out_consumed)
    {
        *data = (const uint8_t *)0;
        return 0U;
    }
    __asm volatile ("" ::: "memory");
    *data = cdc->out_data;
    return cdc->out_length;
}

void Ft232Usbd_CdcRxConsume(const Ft232Usbd *const self)
{
    Ft232UsbdCdcState *const cdc = &self->state->cdc;
    const uint8_t irq_was_enabled = Ft232Usbd_InterruptLock(self);

    cdc->out_consumed = cdc->out_produced;
    cdc->out_length = 0U;
    if ((self->state->configured != 0U) && (cdc->data_enabled != 0U))
    {
        SetEPRxCount(ENDP6, FTDI_USB_CDC_DATA_PACKET_SIZE);
        SetEPRxValid(ENDP6);
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
}

Ft232UsbdResult Ft232Usbd_CdcTxWrite(const Ft232Usbd *const self,
                                     const uint8_t *const data,
                                     uint16_t length)
{
    Ft232UsbdCdcState *const cdc = &self->state->cdc;
    Ft232UsbdResult result = FT232_USBD_OK;
    uint8_t irq_was_enabled;

    if ((length == 0U) || (length > FTDI_USB_CDC_DATA_PACKET_SIZE))
    {
        return FT232_USBD_INVALID_LENGTH;
    }
    if (data == (const uint8_t *)0)
    {
        return FT232_USBD_INVALID_DATA;
    }

    irq_was_enabled = Ft232Usbd_InterruptLock(self);
    if ((self->state->configured == 0U) || (cdc->data_enabled == 0U))
    {
        result = FT232_USBD_NOT_CONFIGURED;
    }
    else if (cdc->in_produced != cdc->in_consumed)
    {
        result = FT232_USBD_TX_BUSY;
    }
    else
    {
        (void)USB_SIL_Write(FTDI_USB_CDC_DATA_IN_EP, (uint8_t *)data, length);
        __asm volatile ("" ::: "memory");
        cdc->in_produced++;
        SetEPTxValid(ENDP7);
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
    return result;
}
#endif

static void ft232_usbd_device_init(void)
{
    uint8_t endpoint;

    pInformation->Current_Configuration = 0U;

    /* 先让 USB 单元退出掉电并复位，再清掉复位期间遗留的端点和事件。 */
    _SetCNTR(CNTR_FRES);
    _SetCNTR(0U);
    for (endpoint = 0U; endpoint < 8U; endpoint++)
    {
        _SetENDPOINT(endpoint,
                     (uint16_t)(_GetENDPOINT(endpoint) &
                                USBD_ENDPOINT_RESET_MASK & EPREG_MASK));
    }
    _SetISTR(0U);
    USB_SIL_Init();

    /* 主机必须看到一次稳定断开，之后才允许内部 1.5 kOhm 上拉宣告设备接入。 */
    ft232_usbd_port_set(0U);
    Delay_Ms(20U);
    ft232_usbd_port_set(1U);
}

static void ft232_usbd_reset(void)
{
    Ft232UsbdState *const state = Ft232Usbd0.state;

    pInformation->Current_Configuration = 0U;
    pInformation->Current_Feature = FtdiUsbConfigurationDescriptor[7];
    pInformation->Current_Interface = FTDI_USB_JTAG_INTERFACE_NUMBER;
    pInformation->Current_AlternateSetting = 0U;

    state->configured = 0U;
    ft232_usbd_cancel_data(state);
#if USB_CDC_ENABLED != 0U
    ft232_usbd_cancel_cdc(&state->cdc);
#endif
    state->bus_reset_event++;

    SetBTABLE(BTABLE_ADDRESS);

    SetEPType(ENDP0, EP_CONTROL);
    SetEPTxStatus(ENDP0, EP_TX_STALL);
    SetEPRxAddr(ENDP0, ENDP0_RXADDR);
    SetEPTxAddr(ENDP0, ENDP0_TXADDR);
    Clear_Status_Out(ENDP0);
    SetEPRxCount(ENDP0, FTDI_USB_EP0_PACKET_SIZE);
    SetEPRxValid(ENDP0);
    ClearDTOG_RX(ENDP0);
    ClearDTOG_TX(ENDP0);

    SetEPType(ENDP1, EP_BULK);
    SetEPTxAddr(ENDP1, ENDP1_TXADDR);
    SetEPTxCount(ENDP1, 0U);
    SetEPTxStatus(ENDP1, EP_TX_DIS);
    SetEPRxStatus(ENDP1, EP_RX_DIS);
    ClearDTOG_RX(ENDP1);
    ClearDTOG_TX(ENDP1);

    SetEPType(ENDP2, EP_BULK);
    SetEPRxAddr(ENDP2, ENDP2_RXADDR);
    SetEPRxCount(ENDP2, FTDI_USB_BULK_PACKET_SIZE);
    SetEPTxStatus(ENDP2, EP_TX_DIS);
    SetEPRxStatus(ENDP2, EP_RX_DIS);
    ClearDTOG_RX(ENDP2);
    ClearDTOG_TX(ENDP2);

    SetEPType(ENDP3, EP_BULK);
    SetEPTxAddr(ENDP3, ENDP3_TXADDR);
    SetEPTxCount(ENDP3, 0U);
    SetEPTxStatus(ENDP3, EP_TX_DIS);
    SetEPRxStatus(ENDP3, EP_RX_DIS);
    ClearDTOG_RX(ENDP3);
    ClearDTOG_TX(ENDP3);

    SetEPType(ENDP4, EP_BULK);
    SetEPRxAddr(ENDP4, ENDP4_RXADDR);
    SetEPRxCount(ENDP4, FTDI_USB_BULK_PACKET_SIZE);
    SetEPTxStatus(ENDP4, EP_TX_DIS);
    SetEPRxStatus(ENDP4, EP_RX_DIS);
    ClearDTOG_RX(ENDP4);
    ClearDTOG_TX(ENDP4);

#if USB_CDC_ENABLED != 0U
    /* CDC 端点在 reset 时只完成静态建表；转发服务 enable 后，
     * SET_CONFIGURATION 才把空闲 EP6 切成 VALID。
     */
    SetEPType(ENDP5, EP_INTERRUPT);
    SetEPTxAddr(ENDP5, ENDP5_TXADDR);
    SetEPTxCount(ENDP5, 0U);
    SetEPTxStatus(ENDP5, EP_TX_DIS);
    SetEPRxStatus(ENDP5, EP_RX_DIS);
    ClearDTOG_RX(ENDP5);
    ClearDTOG_TX(ENDP5);

    SetEPType(ENDP6, EP_BULK);
    SetEPRxAddr(ENDP6, ENDP6_RXADDR);
    SetEPRxCount(ENDP6, FTDI_USB_CDC_DATA_PACKET_SIZE);
    SetEPTxStatus(ENDP6, EP_TX_DIS);
    SetEPRxStatus(ENDP6, EP_RX_DIS);
    ClearDTOG_RX(ENDP6);
    ClearDTOG_TX(ENDP6);

    SetEPType(ENDP7, EP_BULK);
    SetEPTxAddr(ENDP7, ENDP7_TXADDR);
    SetEPTxCount(ENDP7, 0U);
    SetEPTxStatus(ENDP7, EP_TX_DIS);
    SetEPRxStatus(ENDP7, EP_RX_DIS);
    ClearDTOG_RX(ENDP7);
    ClearDTOG_TX(ENDP7);
#endif

    SetDeviceAddress(0U);
}

static void ft232_usbd_status_in(void)
{
}

static void ft232_usbd_status_out(void)
{
}

static RESULT ft232_usbd_data_setup(uint8_t request)
{
    uint8_t *(*copy_routine)(uint16_t) = NULL;

    if ((request == GET_DESCRIPTOR) &&
        (pInformation->USBbmRequestType == USB_REQUEST_DIRECTION_IN) &&
        (pInformation->USBwValue1 == FTDI_USB_DESC_BOS) &&
        (pInformation->USBwValue0 == 0U) &&
        (pInformation->USBwIndex == 0U))
    {
        copy_routine = ft232_usbd_get_bos_descriptor;
    }
    else if ((pInformation->USBbmRequestType == USB_VENDOR_DEVICE_TO_HOST) &&
             (request == FTDI_USB_MS_OS_20_VENDOR_CODE) &&
             (pInformation->USBwValue0 == 0U) &&
             (pInformation->USBwValue1 == 0U) &&
             (pInformation->USBwIndex0 ==
              (uint8_t)FTDI_USB_MS_OS_20_REQUEST_INDEX) &&
             (pInformation->USBwIndex1 == 0U))
    {
        copy_routine = ft232_usbd_get_ms_os_20_descriptor;
    }
#if USB_CDC_ENABLED != 0U
    else if ((pInformation->USBbmRequestType == USB_CLASS_INTERFACE_IN) ||
             (pInformation->USBbmRequestType == USB_CLASS_INTERFACE_OUT))
    {
        return ft232_usbd_cdc_data_setup(request);
    }
#endif
    else if (pInformation->USBbmRequestType == USB_VENDOR_DEVICE_TO_HOST)
    {
        return ft232_usbd_ftdi_data_setup(request);
    }
    else
    {
        return USB_UNSUPPORT;
    }

    pInformation->Ctrl_Info.Usb_wOffset = 0U;
    pInformation->Ctrl_Info.CopyData = copy_routine;
    copy_routine(0U);
    return USB_SUCCESS;
}

static RESULT ft232_usbd_no_data_setup(uint8_t request)
{
#if USB_CDC_ENABLED != 0U
    if (pInformation->USBbmRequestType == USB_CLASS_INTERFACE_OUT)
    {
        return ft232_usbd_cdc_no_data_setup(request);
    }
#endif

    if ((pInformation->USBbmRequestType != USB_VENDOR_HOST_TO_DEVICE) ||
        (pInformation->USBwLength != 0U))
    {
        return USB_UNSUPPORT;
    }

    return ft232_usbd_ftdi_no_data_setup(request);
}

#if USB_CDC_ENABLED != 0U
static RESULT ft232_usbd_cdc_data_setup(uint8_t request)
{
    Ft232UsbdState *const state = Ft232Usbd0.state;

    if ((pInformation->USBwIndex0 != FTDI_USB_CDC_CONTROL_INTERFACE_NUMBER) ||
        (pInformation->USBwIndex1 != 0U) ||
        (pInformation->USBwValue != 0U) ||
        (pInformation->USBwLength != FTDI_USB_CDC_LINE_CODING_SIZE))
    {
        return USB_UNSUPPORT;
    }

    if ((request == FTDI_USB_CDC_GET_LINE_CODING_REQUEST) &&
        (pInformation->USBbmRequestType == USB_CLASS_INTERFACE_IN))
    {
        ft232_usbd_cdc_fixed_line_coding(state);
    }
    else if ((request == FTDI_USB_CDC_SET_LINE_CODING_REQUEST) &&
             (pInformation->USBbmRequestType == USB_CLASS_INTERFACE_OUT))
    {
        /* 必须接完 7 字节 OUT data stage 才能合法 ACK；内容只落在 EP0
         * 临时槽中，不成为 UART 配置的第二事实来源。
         */
        state->control_reply_length = FTDI_USB_CDC_LINE_CODING_SIZE;
    }
    else
    {
        return USB_UNSUPPORT;
    }

    pInformation->Ctrl_Info.Usb_wOffset = 0U;
    pInformation->Ctrl_Info.CopyData = ft232_usbd_get_control_reply;
    ft232_usbd_get_control_reply(0U);
    return USB_SUCCESS;
}

static RESULT ft232_usbd_cdc_no_data_setup(uint8_t request)
{
    if ((pInformation->USBwIndex0 != FTDI_USB_CDC_CONTROL_INTERFACE_NUMBER) ||
        (pInformation->USBwIndex1 != 0U) ||
        (pInformation->USBwLength != 0U))
    {
        return USB_UNSUPPORT;
    }

    if (request == FTDI_USB_CDC_SET_CONTROL_LINE_STATE_REQUEST)
    {
        if ((pInformation->USBwValue1 != 0U) ||
            ((pInformation->USBwValue0 & 0xFCU) != 0U))
        {
            return USB_UNSUPPORT;
        }
        /* USB 层独占主机 DTR/RTS；诊断服务据此在打开串口后发送日志。 */
        Ft232Usbd0.state->cdc.control_line_state = pInformation->USBwValue0;
        return USB_SUCCESS;
    }
    if (request == FTDI_USB_CDC_SEND_BREAK_REQUEST)
    {
        return USB_SUCCESS;
    }
    return USB_UNSUPPORT;
}

static void ft232_usbd_cdc_fixed_line_coding(Ft232UsbdState *const state)
{
    state->control_reply[0] =
        (uint8_t)(UART_FORWARD_BAUD_RATE & 0xFFUL);
    state->control_reply[1] =
        (uint8_t)((UART_FORWARD_BAUD_RATE >> 8U) & 0xFFUL);
    state->control_reply[2] =
        (uint8_t)((UART_FORWARD_BAUD_RATE >> 16U) & 0xFFUL);
    state->control_reply[3] =
        (uint8_t)((UART_FORWARD_BAUD_RATE >> 24U) & 0xFFUL);
    state->control_reply[4] = UART_FORWARD_STOP_BITS;
    state->control_reply[5] = UART_FORWARD_PARITY;
    state->control_reply[6] = UART_FORWARD_DATA_BITS;
    state->control_reply_length = FTDI_USB_CDC_LINE_CODING_SIZE;
}
#endif

static RESULT ft232_usbd_ftdi_data_setup(uint8_t request)
{
    Ft232UsbdState *const state = Ft232Usbd0.state;
    Ft232UsbdChannelState *channel;

    /* WCH EP0 会先 ByteSwap wValue/wIndex；非零字段只能用逻辑字节访问。 */
    if ((pInformation->USBwValue0 != 0U) || (pInformation->USBwValue1 != 0U))
    {
        return USB_UNSUPPORT;
    }

    if (request == FTDI_SIO_READ_EEPROM_REQUEST)
    {
        const uint8_t word = pInformation->USBwIndex0;
        const uint8_t word_count =
            (uint8_t)(sizeof(ft232_eeprom_words) / sizeof(ft232_eeprom_words[0]));

        if ((pInformation->USBwIndex1 != 0U) ||
            (pInformation->USBwLength != FTDI_USB_STATUS_SIZE) ||
            (word >= word_count))
        {
            return USB_UNSUPPORT;
        }
        state->control_reply[0] = (uint8_t)(ft232_eeprom_words[word] & 0x00FFU);
        state->control_reply[1] = (uint8_t)(ft232_eeprom_words[word] >> 8U);
        state->control_reply_length = FTDI_USB_STATUS_SIZE;
        goto reply;
    }

    channel = ft232_usbd_request_channel(state, 0U);
    if (channel == (Ft232UsbdChannelState *)0)
    {
        return USB_UNSUPPORT;
    }

    switch (request)
    {
    case FTDI_SIO_GET_LATENCY_REQUEST:
        if (pInformation->USBwLength != 1U)
        {
            return USB_UNSUPPORT;
        }
        state->control_reply[0] = channel->latency_timer;
        state->control_reply_length = 1U;
        break;

    case FTDI_SIO_POLL_MODEM_STATUS_REQUEST:
        if (pInformation->USBwLength != FTDI_USB_STATUS_SIZE)
        {
            return USB_UNSUPPORT;
        }
        state->control_reply[0] = FTDI_USB_MODEM_STATUS;
        state->control_reply[1] = FTDI_USB_LINE_STATUS;
        state->control_reply_length = FTDI_USB_STATUS_SIZE;
        break;

    case FTDI_SIO_READ_PINS_REQUEST:
        if (pInformation->USBwLength != 1U)
        {
            return USB_UNSUPPORT;
        }
        /* MPSSE GPIO 读命令由数据面处理；bit-bang READ_PINS 暂无额外引脚。 */
        state->control_reply[0] = 0U;
        state->control_reply_length = 1U;
        break;

    default:
        return USB_UNSUPPORT;
    }

reply:
    pInformation->Ctrl_Info.Usb_wOffset = 0U;
    pInformation->Ctrl_Info.CopyData = ft232_usbd_get_control_reply;
    ft232_usbd_get_control_reply(0U);
    return USB_SUCCESS;
}

static RESULT ft232_usbd_ftdi_no_data_setup(uint8_t request)
{
    Ft232UsbdState *const state = Ft232Usbd0.state;
    Ft232UsbdChannelState *channel;
    uint8_t channel_number;
    uint8_t in_endpoint;
    uint8_t out_endpoint;

    if (request == FTDI_SIO_SET_BAUDRATE_REQUEST)
    {
        /*
         * libftdi 会把除数高位、芯片代际和接口信息共同编码进 wIndex，
         * 不能再把低字节收紧成 1/2。两个通道目前都不使用该除数：JTAG
         * 保持固定 GPIO 档位，AUX 尚未绑定 UART。因此参数不落状态，始终
         * 返回零长度 Status-IN ACK，不能让主机把一次兼容性设置判成失败。
         */
        return USB_SUCCESS;
    }

    channel = ft232_usbd_request_channel(
        state, (request == FTDI_SIO_SET_FLOW_CTRL_REQUEST) ? 1U : 0U);
    if (channel == (Ft232UsbdChannelState *)0)
    {
        return USB_UNSUPPORT;
    }
    channel_number = (channel == &state->channel[FT232_USBD_JTAG_CHANNEL])
                         ? FT232_USBD_JTAG_CHANNEL : FT232_USBD_AUX_CHANNEL;
    in_endpoint = (channel_number == FT232_USBD_JTAG_CHANNEL) ? ENDP1 : ENDP3;
    out_endpoint = (channel_number == FT232_USBD_JTAG_CHANNEL) ? ENDP2 : ENDP4;

    switch (request)
    {
    case FTDI_SIO_RESET_REQUEST:
        if ((pInformation->USBwIndex1 != 0U) ||
            (pInformation->USBwValue1 != 0U) ||
            (pInformation->USBwValue0 > (uint8_t)FTDI_SIO_RESET_PURGE_TX))
        {
            return USB_UNSUPPORT;
        }
        if ((channel_number == FT232_USBD_JTAG_CHANNEL) ||
            (pInformation->USBwValue0 == (uint8_t)FTDI_SIO_RESET_SIO))
        {
            /* BL702 通道 A 的三种 reset/purge 都复位整个 MPSSE 内核。
             * CH32 同时取消对应 PMA 邮箱，防止已取消的回复继续发出。
             */
            ft232_usbd_cancel_channel(channel);
            SetEPTxStatus(in_endpoint, EP_TX_NAK);
            if (channel_number == FT232_USBD_JTAG_CHANNEL)
            {
                /* BL702 在控制回调内立即清解析器；CH32 把事件交给主循环。
                 * 事件被处理之前先 NAK，避免吞掉 reset 后紧跟的第一包。
                 */
                SetEPRxStatus(out_endpoint, EP_RX_NAK);
            }
            else if ((state->configured != 0U) && (state->data_enabled != 0U))
            {
                SetEPRxCount(out_endpoint, FTDI_USB_BULK_PACKET_SIZE);
                SetEPRxValid(out_endpoint);
            }
            if (channel_number == FT232_USBD_JTAG_CHANNEL)
            {
                state->sio_reset_event++;
            }
        }
        else if (pInformation->USBwValue0 == (uint8_t)FTDI_SIO_RESET_PURGE_RX)
        {
            ft232_usbd_purge_device_in(channel, in_endpoint);
        }
        else
        {
            channel->out_consumed = channel->out_produced;
            channel->out_length = 0U;
            if ((state->configured != 0U) && (state->data_enabled != 0U))
            {
                SetEPRxCount(out_endpoint, FTDI_USB_BULK_PACKET_SIZE);
                SetEPRxValid(out_endpoint);
            }
        }
        return USB_SUCCESS;

    case FTDI_SIO_SET_LATENCY_REQUEST:
        if ((pInformation->USBwValue1 != 0U) || (pInformation->USBwValue0 == 0U) ||
            (pInformation->USBwIndex1 != 0U))
        {
            return USB_UNSUPPORT;
        }
        channel->latency_timer = pInformation->USBwValue0;
        return USB_SUCCESS;

    case FTDI_SIO_SET_BITMODE_REQUEST:
        if ((pInformation->USBwIndex1 != 0U) ||
            ((pInformation->USBwValue1 != FTDI_SIO_BITMODE_RESET) &&
             (pInformation->USBwValue1 != FTDI_SIO_BITMODE_MPSSE)))
        {
            return USB_UNSUPPORT;
        }
        channel->bit_mode = pInformation->USBwValue1;
        /* BL702 的通道 A SET_BITMODE 只 ACK，不隐式清理 MPSSE 或回包。 */
        if (channel_number != FT232_USBD_JTAG_CHANNEL)
        {
            ft232_usbd_purge_device_in(channel, in_endpoint);
        }
        return USB_SUCCESS;

    case FTDI_SIO_SET_MODEM_CTRL_REQUEST:
    case FTDI_SIO_SET_DATA_REQUEST:
    case FTDI_SIO_SET_EVENT_CHAR_REQUEST:
    case FTDI_SIO_SET_ERROR_CHAR_REQUEST:
        return (pInformation->USBwIndex1 == 0U) ? USB_SUCCESS : USB_UNSUPPORT;

    case FTDI_SIO_SET_FLOW_CTRL_REQUEST:
        /* 流控会借用 wIndex 高字节编码参数，只校验低字节通道号。 */
        return USB_SUCCESS;

    default:
        return USB_UNSUPPORT;
    }
}

static RESULT ft232_usbd_get_interface_setting(uint8_t interface,
                                                uint8_t alternate_setting)
{
    if ((interface >= FTDI_USB_TOTAL_INTERFACE_COUNT) ||
        (alternate_setting != 0U))
    {
        return USB_UNSUPPORT;
    }

    return USB_SUCCESS;
}

static uint8_t *ft232_usbd_get_control_reply(uint16_t length)
{
    Ft232UsbdState *const state = Ft232Usbd0.state;

    if (length == 0U)
    {
        pInformation->Ctrl_Info.Usb_wLength = state->control_reply_length;
        return (uint8_t *)0;
    }
    return &state->control_reply[pInformation->Ctrl_Info.Usb_wOffset];
}

static uint8_t *ft232_usbd_get_device_descriptor(uint16_t length)
{
    if ((pInformation->USBbmRequestType != USB_REQUEST_DIRECTION_IN) ||
        (pInformation->USBwValue0 != 0U) ||
        (pInformation->USBwIndex != 0U))
    {
        pInformation->Ctrl_Info.Usb_wLength = 0U;
        return NULL;
    }

    return Standard_GetDescriptorData(length, &ft232_device_descriptor);
}

static uint8_t *ft232_usbd_get_config_descriptor(uint16_t length)
{
    if ((pInformation->USBbmRequestType != USB_REQUEST_DIRECTION_IN) ||
        (pInformation->USBwValue0 != 0U) ||
        (pInformation->USBwIndex != 0U))
    {
        pInformation->Ctrl_Info.Usb_wLength = 0U;
        return NULL;
    }

    return Standard_GetDescriptorData(length, &ft232_config_descriptor);
}

static uint8_t *ft232_usbd_get_string_descriptor(uint16_t length)
{
    const uint8_t index = pInformation->USBwValue0;
    const uint8_t descriptor_count =
        (uint8_t)(sizeof(ft232_string_descriptors) / sizeof(ft232_string_descriptors[0]));

    if ((pInformation->USBbmRequestType != USB_REQUEST_DIRECTION_IN) ||
        (index >= descriptor_count) ||
        ((index == 0U) && ((pInformation->USBwIndex0 != 0U) ||
                           (pInformation->USBwIndex1 != 0U))) ||
        ((index != 0U) &&
         ((pInformation->USBwIndex0 != USB_LANGUAGE_ID_EN_US_LOW) ||
          (pInformation->USBwIndex1 != USB_LANGUAGE_ID_EN_US_HIGH))))
    {
        pInformation->Ctrl_Info.Usb_wLength = 0U;
        return NULL;
    }

    return Standard_GetDescriptorData(length, &ft232_string_descriptors[index]);
}

static uint8_t *ft232_usbd_get_bos_descriptor(uint16_t length)
{
    return Standard_GetDescriptorData(length, &ft232_bos_descriptor);
}

static uint8_t *ft232_usbd_get_ms_os_20_descriptor(uint16_t length)
{
    return Standard_GetDescriptorData(length, &ft232_ms_os_20_descriptor);
}

static void ft232_usbd_set_configuration(void)
{
    Ft232UsbdState *const state = Ft232Usbd0.state;

    if (pInformation->Current_Configuration == FTDI_USB_CONFIGURATION_VALUE)
    {
        state->configured = 1U;
        ft232_usbd_cancel_channel(&state->channel[FT232_USBD_JTAG_CHANNEL]);
        state->sio_reset_event++;
        SetEPTxStatus(ENDP1, EP_TX_NAK);
        SetEPTxStatus(ENDP3, EP_TX_NAK);
#if USB_CDC_ENABLED != 0U
        SetEPTxStatus(ENDP5, EP_TX_NAK);
        if ((state->cdc.data_enabled != 0U) &&
            (state->cdc.out_produced == state->cdc.out_consumed))
        {
            SetEPRxCount(ENDP6, FTDI_USB_CDC_DATA_PACKET_SIZE);
            SetEPRxValid(ENDP6);
        }
        else
        {
            SetEPRxStatus(ENDP6, EP_RX_NAK);
        }
        SetEPTxStatus(ENDP7, EP_TX_NAK);
#endif
        /* 配置/重新配置和 SIO reset 共用交接：内核复位完成后再接收新流。 */
        SetEPRxStatus(ENDP2, EP_RX_NAK);
        if ((state->data_enabled != 0U) &&
            (state->channel[FT232_USBD_AUX_CHANNEL].out_produced ==
             state->channel[FT232_USBD_AUX_CHANNEL].out_consumed))
        {
            SetEPRxCount(ENDP4, FTDI_USB_BULK_PACKET_SIZE);
            SetEPRxValid(ENDP4);
        }
        else
        {
            SetEPRxStatus(ENDP4, EP_RX_NAK);
        }
    }
    else
    {
        if (state->configured != 0U)
        {
            state->sio_reset_event++;
        }
        state->configured = 0U;
        ft232_usbd_cancel_data(state);
#if USB_CDC_ENABLED != 0U
        ft232_usbd_cancel_cdc(&state->cdc);
#endif
        SetEPTxStatus(ENDP1, EP_TX_DIS);
        SetEPRxStatus(ENDP2, EP_RX_DIS);
        SetEPTxStatus(ENDP3, EP_TX_DIS);
        SetEPRxStatus(ENDP4, EP_RX_DIS);
#if USB_CDC_ENABLED != 0U
        SetEPTxStatus(ENDP5, EP_TX_DIS);
        SetEPRxStatus(ENDP6, EP_RX_DIS);
        SetEPTxStatus(ENDP7, EP_TX_DIS);
#endif
    }
}

static void ft232_usbd_set_address(void)
{
}

static void ft232_usbd_ep1_in(void)
{
    ft232_usbd_in_complete(
        &Ft232Usbd0.state->channel[FT232_USBD_JTAG_CHANNEL], ENDP1);
}

static void ft232_usbd_ep2_out(void)
{
    /* 保留 BL702 已提交 IN 包的生命周期；收包门控只阻止新的 IN 提交。 */
    ft232_usbd_out_receive(
        Ft232Usbd0.state,
        &Ft232Usbd0.state->channel[FT232_USBD_JTAG_CHANNEL],
        FTDI_USB_JTAG_OUT_EP, ENDP2);
}

static void ft232_usbd_ep3_in(void)
{
    ft232_usbd_in_complete(
        &Ft232Usbd0.state->channel[FT232_USBD_AUX_CHANNEL], ENDP3);
}

static void ft232_usbd_ep4_out(void)
{
    ft232_usbd_out_receive(
        Ft232Usbd0.state,
        &Ft232Usbd0.state->channel[FT232_USBD_AUX_CHANNEL],
        FTDI_USB_AUX_OUT_EP, ENDP4);
}

#if USB_CDC_ENABLED != 0U
static void ft232_usbd_ep6_out(void)
{
    Ft232UsbdCdcState *const cdc = &Ft232Usbd0.state->cdc;
    const uint16_t length = GetEPRxCount(ENDP6);

    SetEPRxStatus(ENDP6, EP_RX_NAK);
    if (length > FTDI_USB_CDC_DATA_PACKET_SIZE)
    {
        cdc->fault = FT232_USBD_CDC_FAULT_OUT_LENGTH;
        return;
    }
    if (length == 0U)
    {
        SetEPRxCount(ENDP6, FTDI_USB_CDC_DATA_PACKET_SIZE);
        SetEPRxValid(ENDP6);
        return;
    }
    if (cdc->out_produced != cdc->out_consumed)
    {
        /* EP6 只在邮箱为空时 VALID，命中表示 USB 端点所有权被破坏。 */
        cdc->fault = FT232_USBD_CDC_FAULT_OUT_OVERWRITE;
        return;
    }

    (void)USB_SIL_Read(FTDI_USB_CDC_DATA_OUT_EP, cdc->out_data);
    cdc->out_length = (uint8_t)length;
    __asm volatile ("" ::: "memory");
    cdc->out_produced++;
}

static void ft232_usbd_ep7_in(void)
{
    Ft232UsbdCdcState *const cdc = &Ft232Usbd0.state->cdc;

    cdc->in_consumed = cdc->in_produced;
    SetEPTxStatus(ENDP7, EP_TX_NAK);
}
#endif

static Ft232UsbdChannelState *ft232_usbd_request_channel(
    Ft232UsbdState *const state, uint8_t allow_index_high)
{
    if ((allow_index_high == 0U) && (pInformation->USBwIndex1 != 0U))
    {
        return (Ft232UsbdChannelState *)0;
    }
    if (pInformation->USBwIndex0 == FTDI_USB_JTAG_CHANNEL_INDEX)
    {
        return &state->channel[FT232_USBD_JTAG_CHANNEL];
    }
    if (pInformation->USBwIndex0 == FTDI_USB_AUX_CHANNEL_INDEX)
    {
        return &state->channel[FT232_USBD_AUX_CHANNEL];
    }
    return (Ft232UsbdChannelState *)0;
}

static uint16_t ft232_usbd_rx_peek(Ft232UsbdChannelState *const channel,
                                   const uint8_t **const data)
{
    if (channel->out_produced == channel->out_consumed)
    {
        *data = (const uint8_t *)0;
        return 0U;
    }

    __asm volatile ("" ::: "memory");
    *data = channel->out_data;
    return channel->out_length;
}

static void ft232_usbd_rx_consume(const Ft232Usbd *const self,
                                  Ft232UsbdChannelState *const channel,
                                  uint8_t endpoint)
{
    Ft232UsbdState *const state = self->state;
    const uint8_t irq_was_enabled = Ft232Usbd_InterruptLock(self);
    channel->out_consumed = channel->out_produced;
    if ((state->configured != 0U) && (state->data_enabled != 0U))
    {
        SetEPRxCount(endpoint, FTDI_USB_BULK_PACKET_SIZE);
        SetEPRxValid(endpoint);
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
}

static Ft232UsbdResult ft232_usbd_tx_write(
    const Ft232Usbd *const self,
    Ft232UsbdChannelState *const channel,
    uint8_t endpoint_address,
    uint8_t endpoint,
    const uint8_t *const data,
    uint16_t length)
{
    Ft232UsbdState *const state = self->state;
    Ft232UsbdResult result = FT232_USBD_OK;
    uint8_t irq_was_enabled;

    if ((length < FTDI_USB_STATUS_SIZE) || (length > FTDI_USB_BULK_PACKET_SIZE))
    {
        return FT232_USBD_INVALID_LENGTH;
    }
    if (data == (const uint8_t *)0)
    {
        return FT232_USBD_INVALID_DATA;
    }

    /* PMA 复制期间禁止 USB reset/configuration 回调插入，临界区最多 64 字节。 */
    irq_was_enabled = Ft232Usbd_InterruptLock(self);
    if ((state->configured == 0U) || (state->data_enabled == 0U))
    {
        result = FT232_USBD_NOT_CONFIGURED;
    }
    else if (channel->in_produced != channel->in_consumed)
    {
        result = FT232_USBD_TX_BUSY;
    }
    else
    {
        (void)USB_SIL_Write(endpoint_address, (uint8_t *)data, length);
        __asm volatile ("" ::: "memory");
        channel->in_produced++;
        SetEPTxValid(endpoint);
        if (channel == &state->channel[FT232_USBD_JTAG_CHANNEL])
        {
            state->mpsse_last_data_at = ft232_usbd_systick_low();
            state->mpsse_idle_elapsed = 0U;
        }
    }
    Ft232Usbd_InterruptUnlock(self, irq_was_enabled);
    return result;
}

static void ft232_usbd_in_complete(Ft232UsbdChannelState *const channel,
                                   uint8_t endpoint)
{
    channel->in_consumed = channel->in_produced;
    SetEPTxStatus(endpoint, EP_TX_NAK);
}

static void ft232_usbd_out_receive(Ft232UsbdState *const state,
                                   Ft232UsbdChannelState *const channel,
                                   uint8_t endpoint_address,
                                   uint8_t endpoint)
{
    const uint16_t length = GetEPRxCount(endpoint);

    SetEPRxStatus(endpoint, EP_RX_NAK);
    if (length > FTDI_USB_BULK_PACKET_SIZE)
    {
        state->fault = FT232_USBD_FAULT_OUT_LENGTH;
        return;
    }
    if ((length == 0U) && (endpoint != ENDP2))
    {
        SetEPRxCount(endpoint, FTDI_USB_BULK_PACKET_SIZE);
        SetEPRxValid(endpoint);
        return;
    }
    if (channel->out_produced != channel->out_consumed)
    {
        /* VALID 只在邮箱为空时打开；进入这里说明端点所有权契约已被破坏。 */
        state->fault = FT232_USBD_FAULT_OUT_OVERWRITE;
        return;
    }

    /* 通道 A 也发布零长度 OUT 事件：BL702 在读包前检查满批次容量，
     * 满 4 KiB 时的下一次 ZLP 也会触发执行；普通 ZLP 由 service 忽略。
     */
    if (length != 0U)
    {
        (void)USB_SIL_Read(endpoint_address, channel->out_data);
    }
    channel->out_length = (uint8_t)length;
    __asm volatile ("" ::: "memory");
    channel->out_produced++;
}

static void ft232_usbd_cancel_data(Ft232UsbdState *const state)
{
    ft232_usbd_cancel_channel(&state->channel[FT232_USBD_JTAG_CHANNEL]);
    ft232_usbd_cancel_channel(&state->channel[FT232_USBD_AUX_CHANNEL]);
}

#if USB_CDC_ENABLED != 0U
static void ft232_usbd_cancel_cdc(Ft232UsbdCdcState *const cdc)
{
    cdc->out_produced = cdc->out_consumed;
    cdc->in_consumed = cdc->in_produced;
    cdc->out_length = 0U;
    cdc->control_line_state = 0U;
}
#endif

static uint32_t ft232_usbd_systick_low(void)
{
    return SystemTimebase_Now(&SystemTimebase0);
}

static void ft232_usbd_cancel_channel(Ft232UsbdChannelState *const channel)
{
    /* 两个序号分别由 ISR 和主循环拥有；USB reset 发生在 ISR 边界，此处只让
     * ISR 自己的生产/完成端追上另一端，旧 PMA 事务随端点复位一起失效。
     */
    channel->out_produced = channel->out_consumed;
    channel->in_consumed = channel->in_produced;
    channel->out_length = 0U;
}

static void ft232_usbd_purge_device_in(Ft232UsbdChannelState *const channel,
                                       uint8_t endpoint)
{
    SetEPTxStatus(endpoint, EP_TX_NAK);
    channel->in_consumed = channel->in_produced;
}

uint8_t Ft232Usbd_InterruptLock(const Ft232Usbd *const self)
{
    const IRQn_Type channel = self->config->interrupt.NVIC_IRQChannel;
    const uint8_t was_enabled = (NVIC_GetStatusIRQ(channel) != 0U) ? 1U : 0U;

    NVIC_DisableIRQ(channel);
    return was_enabled;
}

void Ft232Usbd_InterruptUnlock(const Ft232Usbd *const self, uint8_t token)
{
    if (token != 0U)
    {
        NVIC_EnableIRQ(self->config->interrupt.NVIC_IRQChannel);
    }
}

static void ft232_usbd_port_set(uint8_t connected)
{
    if (connected != 0U)
    {
        _SetCNTR((uint16_t)(_GetCNTR() & (uint16_t)~CNTR_PDWN));
        BoardGpio_UsbdPinsRelease(Ft232Usbd0.config->gpio);
        EXTEN->EXTEN_CTR |= EXTEN_USBD_PU_EN;
    }
    else
    {
        EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
        _SetCNTR((uint16_t)(_GetCNTR() | CNTR_PDWN));
        BoardGpio_UsbdPinsDriveLow(Ft232Usbd0.config->gpio);
    }
}

static void ft232_usbd_interrupt_service(void)
{
    /* WCH USBD 的 EP0 RX 块数位偶尔会被硬件改写，按厂商库要求先恢复。 */
    if ((*_pEPRxCount(ENDP0) & USBD_EP0_RX_BLOCK_MASK) != Ep0RxBlks)
    {
        *_pEPRxCount(ENDP0) |= (uint32_t)(Ep0RxBlks & USBD_EP0_RX_BLOCK_MASK);
    }

    wIstr = _GetISTR();
    if ((wIstr & ISTR_CTR & wInterrupt_Mask) != 0U)
    {
        CTR_LP();
    }

    wIstr = _GetISTR();
    if ((wIstr & ISTR_RESET & wInterrupt_Mask) != 0U)
    {
        _SetISTR((uint16_t)CLR_RESET);
        Device_Property.Reset();
    }
}

void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    ft232_usbd_interrupt_service();
}

#if USB_CDC_ENABLED != 0U
_Static_assert(EP_NUM == 8U,
               "FT2232 plus CDC endpoint topology requires EP0 through EP7");
_Static_assert(FTDI_USB_INTERFACE_COUNT == 2U,
               "FT2232 control state requires exactly two channels");
_Static_assert(sizeof(ft232_eeprom_words) == 128U,
               "FT2232 EEPROM image must contain 64 words");
_Static_assert(ENDP5_TXADDR >= ENDP4_RXADDR + FTDI_USB_BULK_PACKET_SIZE,
               "CDC notification PMA overlaps FTDI channel B RX");
_Static_assert(ENDP6_RXADDR >=
                   ENDP5_TXADDR + FTDI_USB_CDC_NOTIFICATION_PACKET_SIZE,
               "CDC OUT PMA overlaps CDC notification IN");
_Static_assert(ENDP7_TXADDR >= ENDP6_RXADDR + FTDI_USB_CDC_DATA_PACKET_SIZE,
               "CDC IN PMA overlaps CDC OUT");
_Static_assert(ENDP7_TXADDR + FTDI_USB_CDC_DATA_PACKET_SIZE <= USBD_PMA_SIZE,
               "USB PMA allocation exceeds the 512-byte packet memory");
#else
_Static_assert(EP_NUM == 5U,
               "FT2232 endpoint topology requires EP0 through EP4");
_Static_assert(ENDP4_RXADDR + FTDI_USB_BULK_PACKET_SIZE <= USBD_PMA_SIZE,
               "USB PMA allocation exceeds the 512-byte packet memory");
#endif
