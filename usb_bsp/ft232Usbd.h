#ifndef CH32_FT232_USBD_H
#define CH32_FT232_USBD_H

#include <stdint.h>

#include "ch32v20x.h"
#include "ft232Descriptor.h"

struct BoardGpio;

typedef enum
{
    FT232_USBD_FAULT_NONE = 0,
    FT232_USBD_FAULT_OUT_LENGTH,
    FT232_USBD_FAULT_OUT_OVERWRITE
} Ft232UsbdFault;

typedef struct
{
    uint32_t usb_clock_source;
    NVIC_InitTypeDef interrupt;
    const struct BoardGpio *const gpio;
} Ft232UsbdConfig;

/* 每个 FT2232 通道各有一个 64 字节静态邮箱：ISR 填满后不重开端点，
 * 主循环消费后再 VALID。通道 A/B 的端点状态互不覆盖。
 */
typedef struct
{
    uint8_t out_data[FTDI_USB_BULK_PACKET_SIZE];
    volatile uint8_t out_length;
    volatile uint8_t out_produced;
    volatile uint8_t out_consumed;
    volatile uint8_t in_produced;
    volatile uint8_t in_consumed;
    uint8_t latency_timer;
    uint8_t bit_mode;
} Ft232UsbdChannelState;

#if USB_CDC_ENABLED != 0U
typedef enum
{
    FT232_USBD_CDC_FAULT_NONE = 0,
    FT232_USBD_CDC_FAULT_OUT_LENGTH,
    FT232_USBD_CDC_FAULT_OUT_OVERWRITE
} Ft232UsbdCdcFault;

typedef struct
{
    uint8_t out_data[FTDI_USB_CDC_DATA_PACKET_SIZE];
    volatile uint8_t out_length;
    volatile uint8_t out_produced;
    volatile uint8_t out_consumed;
    volatile uint8_t in_produced;
    volatile uint8_t in_consumed;
    volatile uint8_t data_enabled;
    volatile uint8_t fault;
    volatile uint8_t control_line_state;
} Ft232UsbdCdcState;
#endif

typedef struct
{
    Ft232UsbdChannelState channel[FTDI_USB_INTERFACE_COUNT];
    /* JTAG 核只交付裸 MPSSE 回复；FTDI 状态头由 USB 边界在此补齐。 */
    uint8_t mpsse_in_packet[FTDI_USB_BULK_PACKET_SIZE];
    /* 同一时刻 EP0 只有一个事务；打开 UART 时也承载 CDC line coding。 */
    uint8_t control_reply[FTDI_USB_CONTROL_REPLY_SIZE];
    uint8_t control_reply_length;
    volatile uint8_t configured;
    volatile uint8_t data_enabled;
    volatile uint8_t bus_reset_event;
    volatile uint8_t sio_reset_event;
    volatile uint8_t host_rx_purge_event;
    volatile uint8_t host_tx_purge_event;
    volatile uint8_t fault;
#if USB_CDC_ENABLED != 0U
    Ft232UsbdCdcState cdc;
#endif
    uint32_t mpsse_last_data_at;
    uint8_t mpsse_idle_elapsed;
} Ft232UsbdState;

_Static_assert(sizeof(Ft232UsbdState) <= 256U,
               "FT232 USBD state exceeds its static RAM budget");

typedef struct
{
    const Ft232UsbdConfig *const config;
    Ft232UsbdState *const state;
} Ft232Usbd;

typedef struct
{
    uint8_t bus_reset;
    uint8_t sio_reset;
    uint8_t host_rx_purge;
    uint8_t host_tx_purge;
    uint8_t fault;
} Ft232UsbdEvents;

typedef enum
{
    FT232_USBD_OK = 0,
    FT232_USBD_NOT_CONFIGURED,
    FT232_USBD_TX_BUSY,
    FT232_USBD_INVALID_LENGTH,
    FT232_USBD_INVALID_DATA
} Ft232UsbdResult;

/* system_ch32v20x.c 固定 PLL=144 MHz，USBD 必须分频到 48 MHz。
 * WCH 回调没有 self 参数，因此本项目只发布这一份编译期装配的 USBD 实例。
 */
extern const Ft232Usbd Ft232Usbd0;

void Ft232Usbd_Init(const Ft232Usbd *self);
void Ft232Usbd_InterruptInit(const Ft232Usbd *self);

/* GPIO/JTAG 就绪后才打开 bulk 数据面。枚举和 FTDI 控制请求不依赖此开关。 */
void Ft232Usbd_DataEnable(const Ft232Usbd *self);
/* 仅在无接收事务、无真实 TX 时调用。对齐 BL702：真实回复后超过
 * 1 ms 可回 31 60；空包本身不重置计时，已提交的 IN 包不因 OUT 撤回。
 */
void Ft232Usbd_MpsseService(const Ft232Usbd *self);
uint8_t Ft232Usbd_IsConfigured(const Ft232Usbd *self);
void Ft232Usbd_GetEvents(const Ft232Usbd *self, Ft232UsbdEvents *events);

/* 通道 A OUT 数据就是裸 MPSSE，Peek 后必须整包 Consume；未消费时 EP2 保持 NAK。
 * Peek 的 data=NULL 表示无包；data 非空、长度为零则是 OUT ZLP。
 * MpsseTxWrite 借用至多两段共 1..62 字节裸回复，补 31 60 后复制进 PMA。
 * 返回 OK 后调用方即可释放源数据；此写接口不接受空回复。
 */
uint16_t Ft232Usbd_MpsseRxPeek(const Ft232Usbd *self, const uint8_t **data);
void Ft232Usbd_MpsseRxConsume(const Ft232Usbd *self);
Ft232UsbdResult Ft232Usbd_MpsseTxWrite(const Ft232Usbd *self,
                                       const uint8_t *head, uint16_t head_length,
                                       const uint8_t *tail, uint16_t tail_length);

/* 通道 B 只提供独立的原始 FTDI 数据面；UART 引脚和收发器尚未由板级配置指定，
 * 因而本模块不擅自消费数据，也不伪造 UART 状态。
 */
uint16_t Ft232Usbd_AuxRxPeek(const Ft232Usbd *self, const uint8_t **data);
void Ft232Usbd_AuxRxConsume(const Ft232Usbd *self);
Ft232UsbdResult Ft232Usbd_AuxTxWrite(const Ft232Usbd *self,
                                     const uint8_t *data,
                                     uint16_t length);

#if USB_CDC_ENABLED != 0U
/* CDC 只向转发服务发布裸字节邮箱。USB 中断拥有生产端，主循环消费后才
 * 重新 VALID EP6；EP7 写入完成前返回 TX_BUSY。
 */
void Ft232Usbd_CdcDataEnable(const Ft232Usbd *self);
uint8_t Ft232Usbd_CdcIsReady(const Ft232Usbd *self);
uint8_t Ft232Usbd_CdcIsOpen(const Ft232Usbd *self);
Ft232UsbdCdcFault Ft232Usbd_CdcGetFault(const Ft232Usbd *self);
uint16_t Ft232Usbd_CdcRxPeek(const Ft232Usbd *self, const uint8_t **data);
void Ft232Usbd_CdcRxConsume(const Ft232Usbd *self);
Ft232UsbdResult Ft232Usbd_CdcTxWrite(const Ft232Usbd *self,
                                     const uint8_t *data,
                                     uint16_t length);
#endif

/* Gowin 整页 MPSSE 和擦除时钟期间只屏蔽 USBD 中断，PMA 仍在硬件中 NAK。
 * token 必须原样交回 Unlock，不能无条件重新打开原本关闭的中断。
 */
uint8_t Ft232Usbd_InterruptLock(const Ft232Usbd *self);
void Ft232Usbd_InterruptUnlock(const Ft232Usbd *self, uint8_t token);

#endif /* CH32_FT232_USBD_H */
