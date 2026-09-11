#ifndef CH32_FT232_DESCRIPTOR_H
#define CH32_FT232_DESCRIPTOR_H

#include <stdint.h>

#include "UartForwardConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 保留 BL702 已验证的 FT2232D 设备身份。FT2232 的通道号从 1 开始，
 * USB interface 和端点则从 0 开始，两组编号不能混用。
 */
#define FTDI_USB_VENDOR_ID             (0x0403U)
#define FTDI_USB_PRODUCT_ID            (0x6010U)
#define FTDI_USB_DEVICE_RELEASE        (0x0500U)
#define FTDI_USB_CONFIGURATION_VALUE   (0x01U)
/* 前两个接口仍属于 FT2232；后两个接口组成独立的 CDC ACM 功能。 */
#define FTDI_USB_INTERFACE_COUNT       (2U)
#define FTDI_USB_TOTAL_INTERFACE_COUNT (4U)
#define FTDI_USB_JTAG_INTERFACE_NUMBER (0x00U)
#define FTDI_USB_AUX_INTERFACE_NUMBER  (0x01U)
#define FTDI_USB_CDC_CONTROL_INTERFACE_NUMBER (0x02U)
#define FTDI_USB_CDC_DATA_INTERFACE_NUMBER    (0x03U)
#define FTDI_USB_JTAG_CHANNEL_INDEX    (0x01U)
#define FTDI_USB_AUX_CHANNEL_INDEX     (0x02U)

/* CH32 USB Full Speed 单包上限为 64 字节。IN 的前两个字节属于 FTDI 状态。 */
#define FTDI_USB_EP0_PACKET_SIZE       (64U)
#define FTDI_USB_BULK_PACKET_SIZE      (64U)
#define FTDI_USB_STATUS_SIZE           (2U)
#define FTDI_USB_IN_DATA_SIZE          (FTDI_USB_BULK_PACKET_SIZE - FTDI_USB_STATUS_SIZE)
#define FTDI_USB_JTAG_IN_EP            (0x81U)
#define FTDI_USB_JTAG_OUT_EP           (0x02U)
#define FTDI_USB_AUX_IN_EP             (0x83U)
#define FTDI_USB_AUX_OUT_EP            (0x04U)
#define FTDI_USB_CDC_NOTIFICATION_IN_EP (0x85U)
#define FTDI_USB_CDC_DATA_OUT_EP        (0x06U)
#define FTDI_USB_CDC_DATA_IN_EP         (0x87U)
#define FTDI_USB_CDC_NOTIFICATION_PACKET_SIZE (16U)
#define FTDI_USB_CDC_DATA_PACKET_SIZE   UART_FORWARD_COPY_CHUNK_SIZE
#define FTDI_USB_MODEM_STATUS          (0x31U)
#define FTDI_USB_LINE_STATUS           (0x60U)

/* CDC 串口格式是编译期事实。SET_LINE_CODING 只完成 USB 控制传输，
 * 不允许主机改写后续 UART 初始化所使用的参数。
 */
#define FTDI_USB_CDC_SET_LINE_CODING_REQUEST        (0x20U)
#define FTDI_USB_CDC_GET_LINE_CODING_REQUEST        (0x21U)
#define FTDI_USB_CDC_SET_CONTROL_LINE_STATE_REQUEST (0x22U)
#define FTDI_USB_CDC_SEND_BREAK_REQUEST             (0x23U)
#define FTDI_USB_CDC_LINE_CODING_SIZE                (7U)

/* 标准描述符类型。USB 驱动按 GET_DESCRIPTOR 的 wValue 高字节选择。 */
#define FTDI_USB_DESC_DEVICE           (0x01U)
#define FTDI_USB_DESC_CONFIGURATION    (0x02U)
#define FTDI_USB_DESC_STRING           (0x03U)
#define FTDI_USB_DESC_BOS              (0x0FU)

#define FTDI_USB_DEVICE_DESC_SIZE      (18U)
/* 原 FT2232 配置为 55 字节，追加 IAD 与 CDC ACM 功能共 66 字节。 */
#define FTDI_USB_CONFIG_DESC_SIZE      (55U + 66U)
#define FTDI_USB_LANG_DESC_SIZE        (4U)
#define FTDI_USB_MANUFACTURER_DESC_SIZE (10U)
#define FTDI_USB_PRODUCT_DESC_SIZE     (28U)
#define FTDI_USB_SERIAL_DESC_SIZE      (46U)
#define FTDI_USB_BOS_DESC_SIZE         (33U)
#define FTDI_USB_MS_OS_20_DESC_SIZE    (46U)

/* bcdUSB=2.10 用于让 Windows 查询 BOS/MS OS 2.0；物理链路仍为 Full Speed。 */
#define FTDI_USB_MS_OS_20_VENDOR_CODE  (0x20U)
#define FTDI_USB_MS_OS_20_REQUEST_INDEX (0x0007U)

/* 枚举完成后 FTDI 主机库常用的 vendor request。通道号放 wIndex=1/2。 */
#define FTDI_SIO_RESET_REQUEST             (0x00U)
#define FTDI_SIO_SET_MODEM_CTRL_REQUEST    (0x01U)
#define FTDI_SIO_SET_FLOW_CTRL_REQUEST     (0x02U)
#define FTDI_SIO_SET_BAUDRATE_REQUEST      (0x03U)
#define FTDI_SIO_SET_DATA_REQUEST          (0x04U)
#define FTDI_SIO_POLL_MODEM_STATUS_REQUEST (0x05U)
#define FTDI_SIO_SET_EVENT_CHAR_REQUEST    (0x06U)
#define FTDI_SIO_SET_ERROR_CHAR_REQUEST    (0x07U)
#define FTDI_SIO_SET_LATENCY_REQUEST       (0x09U)
#define FTDI_SIO_GET_LATENCY_REQUEST       (0x0AU)
#define FTDI_SIO_SET_BITMODE_REQUEST       (0x0BU)
#define FTDI_SIO_READ_PINS_REQUEST         (0x0CU)
#define FTDI_SIO_READ_EEPROM_REQUEST       (0x90U)

#define FTDI_SIO_RESET_SIO                 (0x0000U)
#define FTDI_SIO_RESET_PURGE_RX            (0x0001U)
#define FTDI_SIO_RESET_PURGE_TX            (0x0002U)
#define FTDI_SIO_BITMODE_RESET             (0x00U)
#define FTDI_SIO_BITMODE_MPSSE             (0x02U)

/* 所有数组均显式进入 Flash。若 USB 驱动需要唯一序列号，应在 EP0 临时缓冲区
 * 生成 string index 3 的替代回复，不要修改这些只读描述符。
 */
#define FTDI_USB_DESCRIPTOR_FLASH __attribute__((section(".rodata.usb_descriptor")))

extern const uint8_t FtdiUsbDeviceDescriptor[FTDI_USB_DEVICE_DESC_SIZE];
extern const uint8_t FtdiUsbConfigurationDescriptor[FTDI_USB_CONFIG_DESC_SIZE];
extern const uint8_t FtdiUsbLanguageDescriptor[FTDI_USB_LANG_DESC_SIZE];
extern const uint8_t FtdiUsbManufacturerDescriptor[FTDI_USB_MANUFACTURER_DESC_SIZE];
extern const uint8_t FtdiUsbProductDescriptor[FTDI_USB_PRODUCT_DESC_SIZE];
extern const uint8_t FtdiUsbSerialDescriptor[FTDI_USB_SERIAL_DESC_SIZE];
extern const uint8_t FtdiUsbBosDescriptor[FTDI_USB_BOS_DESC_SIZE];
extern const uint8_t FtdiUsbMsOs20Descriptor[FTDI_USB_MS_OS_20_DESC_SIZE];

#ifdef __cplusplus
}
#endif

#endif /* CH32_FT232_DESCRIPTOR_H */
