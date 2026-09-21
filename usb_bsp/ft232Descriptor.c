#include "ft232Descriptor.h"
#include "ftdiUsbBuildSerial.h"

#define USB_U16_LOW(value)  ((uint8_t)((uint16_t)(value) & 0xFFU))
#define USB_U16_HIGH(value) ((uint8_t)(((uint16_t)(value) >> 8U) & 0xFFU))

#define FTDI_MS_OS_20_SET_HEADER_SIZE          (10U)
#define FTDI_MS_OS_20_CONFIG_SUBSET_HEADER_SIZE (8U)
#define FTDI_MS_OS_20_FUNCTION_SUBSET_HEADER_SIZE (8U)
#define FTDI_MS_OS_20_COMPATIBLE_ID_SIZE       (20U)
#define FTDI_MS_OS_20_REGISTRY_PROPERTY_SIZE   (132U)
#define FTDI_MS_OS_20_FUNCTION_SUBSET_SIZE \
    (FTDI_MS_OS_20_FUNCTION_SUBSET_HEADER_SIZE + \
     FTDI_MS_OS_20_COMPATIBLE_ID_SIZE + \
     FTDI_MS_OS_20_REGISTRY_PROPERTY_SIZE)
#define FTDI_MS_OS_20_CONFIG_SUBSET_SIZE \
    (FTDI_MS_OS_20_CONFIG_SUBSET_HEADER_SIZE + \
     FTDI_MS_OS_20_FUNCTION_SUBSET_SIZE)

const uint8_t FtdiUsbDeviceDescriptor[FTDI_USB_DEVICE_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    0x12U, FTDI_USB_DESC_DEVICE,
    0x10U, 0x02U,                         /* bcdUSB 2.10：允许查询 BOS。 */
    FTDI_USB_DEVICE_CLASS,
    FTDI_USB_DEVICE_SUBCLASS,
    FTDI_USB_DEVICE_PROTOCOL,
    FTDI_USB_EP0_PACKET_SIZE,
    USB_U16_LOW(FTDI_USB_VENDOR_ID), USB_U16_HIGH(FTDI_USB_VENDOR_ID),
    USB_U16_LOW(FTDI_USB_PRODUCT_ID), USB_U16_HIGH(FTDI_USB_PRODUCT_ID),
    USB_U16_LOW(FTDI_USB_DEVICE_RELEASE), USB_U16_HIGH(FTDI_USB_DEVICE_RELEASE),
    0x01U,                                /* iManufacturer */
    0x02U,                                /* iProduct */
    0x03U,                                /* iSerialNumber */
    0x01U                                 /* bNumConfigurations */
};

const uint8_t FtdiUsbConfigurationDescriptor[FTDI_USB_CONFIG_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    0x09U, FTDI_USB_DESC_CONFIGURATION,
    USB_U16_LOW(FTDI_USB_CONFIG_DESC_SIZE), USB_U16_HIGH(FTDI_USB_CONFIG_DESC_SIZE),
    FTDI_USB_TOTAL_INTERFACE_COUNT,
    FTDI_USB_CONFIGURATION_VALUE,
    0x00U,
    0x80U,                                /* 总线供电，不声明远程唤醒。 */
    0x32U,                                /* 100 mA，单位为 2 mA。 */

    0x09U, 0x04U,                         /* Interface descriptor */
    FTDI_USB_JTAG_INTERFACE_NUMBER,
    0x00U,
    0x02U,
    0xFFU, 0xFFU, 0xFFU,
    0x02U,                                /* iInterface：沿用 Dual RS232-HS 产品名。 */

    0x07U, 0x05U,                         /* Bulk IN */
    FTDI_USB_JTAG_IN_EP,
    0x02U,
    USB_U16_LOW(FTDI_USB_BULK_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_BULK_PACKET_SIZE),
    0x00U,

    0x07U, 0x05U,                         /* Bulk OUT */
    FTDI_USB_JTAG_OUT_EP,
    0x02U,
    USB_U16_LOW(FTDI_USB_BULK_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_BULK_PACKET_SIZE),
    0x00U,

    0x09U, 0x04U,                         /* Interface B descriptor */
    FTDI_USB_AUX_INTERFACE_NUMBER,
    0x00U,
    0x02U,
    0xFFU, 0xFFU, 0xFFU,
    0x00U,                                /* BL702 的通道 B 不挂字符串。 */

    0x07U, 0x05U,                         /* Bulk IN */
    FTDI_USB_AUX_IN_EP,
    0x02U,
    USB_U16_LOW(FTDI_USB_BULK_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_BULK_PACKET_SIZE),
    0x00U,

    0x07U, 0x05U,                         /* Bulk OUT */
    FTDI_USB_AUX_OUT_EP,
    0x02U,
    USB_U16_LOW(FTDI_USB_BULK_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_BULK_PACKET_SIZE),
    0x00U,

#if USB_CDC_ENABLED != 0U
    /* IAD 把接口 2/3 明确归为一个 CDC ACM 功能，同时保留前两个 FTDI
     * vendor interface 的编号和端点不变。
     */
    0x08U, 0x0BU,                         /* Interface Association */
    FTDI_USB_CDC_CONTROL_INTERFACE_NUMBER,
    0x02U,
    0x02U, 0x02U, 0x01U,
    0x00U,

    0x09U, 0x04U,                         /* CDC communication interface */
    FTDI_USB_CDC_CONTROL_INTERFACE_NUMBER,
    0x00U,
    0x01U,
    0x02U, 0x02U, 0x01U,
    0x00U,

    0x05U, 0x24U, 0x00U, 0x10U, 0x01U,  /* CDC Header 1.10 */
    0x05U, 0x24U, 0x01U, 0x00U,
    FTDI_USB_CDC_DATA_INTERFACE_NUMBER,  /* Call Management */
    0x04U, 0x24U, 0x02U, 0x02U,         /* ACM：支持 line coding。 */
    0x05U, 0x24U, 0x06U,
    FTDI_USB_CDC_CONTROL_INTERFACE_NUMBER,
    FTDI_USB_CDC_DATA_INTERFACE_NUMBER,  /* Union */

    0x07U, 0x05U,                         /* CDC notification IN */
    FTDI_USB_CDC_NOTIFICATION_IN_EP,
    0x03U,
    USB_U16_LOW(FTDI_USB_CDC_NOTIFICATION_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_CDC_NOTIFICATION_PACKET_SIZE),
    0x10U,

    0x09U, 0x04U,                         /* CDC data interface */
    FTDI_USB_CDC_DATA_INTERFACE_NUMBER,
    0x00U,
    0x02U,
    0x0AU, 0x00U, 0x00U,
    0x00U,

    0x07U, 0x05U,                         /* CDC Bulk OUT */
    FTDI_USB_CDC_DATA_OUT_EP,
    0x02U,
    USB_U16_LOW(FTDI_USB_CDC_DATA_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_CDC_DATA_PACKET_SIZE),
    0x00U,

    0x07U, 0x05U,                         /* CDC Bulk IN */
    FTDI_USB_CDC_DATA_IN_EP,
    0x02U,
    USB_U16_LOW(FTDI_USB_CDC_DATA_PACKET_SIZE),
    USB_U16_HIGH(FTDI_USB_CDC_DATA_PACKET_SIZE),
    0x00U
#endif
};

const uint8_t FtdiUsbLanguageDescriptor[FTDI_USB_LANG_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    FTDI_USB_LANG_DESC_SIZE, FTDI_USB_DESC_STRING, 0x09U, 0x04U
};

const uint8_t FtdiUsbManufacturerDescriptor[FTDI_USB_MANUFACTURER_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    FTDI_USB_MANUFACTURER_DESC_SIZE, FTDI_USB_DESC_STRING,
    'C', 0x00U, 'H', 0x00U, '3', 0x00U, '2', 0x00U
};

const uint8_t FtdiUsbProductDescriptor[FTDI_USB_PRODUCT_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    FTDI_USB_PRODUCT_DESC_SIZE, FTDI_USB_DESC_STRING,
    'D', 0x00U, 'u', 0x00U, 'a', 0x00U, 'l', 0x00U, ' ', 0x00U,
    'R', 0x00U, 'S', 0x00U, '2', 0x00U, '3', 0x00U, '2', 0x00U,
    '-', 0x00U, 'H', 0x00U, 'S', 0x00U
};

const uint8_t FtdiUsbSerialDescriptor[FTDI_USB_SERIAL_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    FTDI_USB_SERIAL_DESC_SIZE, FTDI_USB_DESC_STRING,
    'C', 0x00U, 'H', 0x00U, '3', 0x00U, '2', 0x00U, '_', 0x00U,
    'F', 0x00U, 'T', 0x00U, 'D', 0x00U, 'I', 0x00U, '_', 0x00U,
    FTDI_USB_BUILD_SERIAL_SUFFIX_UTF16
};

/* Microsoft OS 2.0 平台能力：Windows 将接口 0 自动绑定到 WinUSB。 */
const uint8_t FtdiUsbBosDescriptor[FTDI_USB_BOS_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    0x05U, FTDI_USB_DESC_BOS, 0x21U, 0x00U, 0x01U,
    0x1CU, 0x10U, 0x05U, 0x00U,
    0xD8U, 0xDDU, 0x60U, 0xDFU, 0x45U, 0x89U, 0x4CU, 0xC7U,
    0x9CU, 0xD2U, 0x65U, 0x9DU, 0x9EU, 0x64U, 0x8AU, 0x9FU,
    0x00U, 0x00U, 0x03U, 0x06U,
    USB_U16_LOW(FTDI_USB_MS_OS_20_DESC_SIZE),
    USB_U16_HIGH(FTDI_USB_MS_OS_20_DESC_SIZE),
    FTDI_USB_MS_OS_20_VENDOR_CODE,
    0x00U
};

const uint8_t FtdiUsbMsOs20Descriptor[FTDI_USB_MS_OS_20_DESC_SIZE]
    FTDI_USB_DESCRIPTOR_FLASH = {
    0x0AU, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x03U, 0x06U,
    USB_U16_LOW(FTDI_USB_MS_OS_20_DESC_SIZE),
    USB_U16_HIGH(FTDI_USB_MS_OS_20_DESC_SIZE),

    0x08U, 0x00U, 0x01U, 0x00U,
    0x00U, 0x00U,
    USB_U16_LOW(FTDI_MS_OS_20_CONFIG_SUBSET_SIZE),
    USB_U16_HIGH(FTDI_MS_OS_20_CONFIG_SUBSET_SIZE),

    0x08U, 0x00U, 0x02U, 0x00U,
    FTDI_USB_JTAG_INTERFACE_NUMBER, 0x00U,
    USB_U16_LOW(FTDI_MS_OS_20_FUNCTION_SUBSET_SIZE),
    USB_U16_HIGH(FTDI_MS_OS_20_FUNCTION_SUBSET_SIZE),

    0x14U, 0x00U, 0x03U, 0x00U,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,

    /* MS OS 2.0 registry property：给 interface 0 注册稳定的设备接口路径。
     * wPropertyDataType=7 表示 REG_MULTI_SZ，属性名和 GUID 均为 UTF-16LE。
     */
    USB_U16_LOW(FTDI_MS_OS_20_REGISTRY_PROPERTY_SIZE),
    USB_U16_HIGH(FTDI_MS_OS_20_REGISTRY_PROPERTY_SIZE),
    0x04U, 0x00U,
    0x07U, 0x00U,
    0x2AU, 0x00U,
    'D', 0x00U, 'e', 0x00U, 'v', 0x00U, 'i', 0x00U,
    'c', 0x00U, 'e', 0x00U, 'I', 0x00U, 'n', 0x00U,
    't', 0x00U, 'e', 0x00U, 'r', 0x00U, 'f', 0x00U,
    'a', 0x00U, 'c', 0x00U, 'e', 0x00U, 'G', 0x00U,
    'U', 0x00U, 'I', 0x00U, 'D', 0x00U, 's', 0x00U,
    0x00U, 0x00U,
    0x50U, 0x00U,
    '{', 0x00U, '2', 0x00U, 'E', 0x00U, '7', 0x00U,
    '3', 0x00U, 'E', 0x00U, 'A', 0x00U, 'F', 0x00U,
    '0', 0x00U, '-', 0x00U, '0', 0x00U, '5', 0x00U,
    '9', 0x00U, 'B', 0x00U, '-', 0x00U, '4', 0x00U,
    '0', 0x00U, '6', 0x00U, 'E', 0x00U, '-', 0x00U,
    'B', 0x00U, 'F', 0x00U, '7', 0x00U, 'D', 0x00U,
    '-', 0x00U, '5', 0x00U, '2', 0x00U, '9', 0x00U,
    '6', 0x00U, '3', 0x00U, '4', 0x00U, 'C', 0x00U,
    '9', 0x00U, '0', 0x00U, '8', 0x00U, '4', 0x00U,
    'B', 0x00U, '}', 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U
};

_Static_assert(FTDI_USB_BULK_PACKET_SIZE == 64U,
               "CH32 USB Full Speed bulk endpoint must use 64-byte packets");
_Static_assert(FTDI_USB_IN_DATA_SIZE == 62U,
               "FTDI IN packet reserves two status bytes");
_Static_assert(FTDI_USB_MS_OS_20_DESC_SIZE ==
                   (FTDI_MS_OS_20_SET_HEADER_SIZE +
                    FTDI_MS_OS_20_CONFIG_SUBSET_SIZE),
               "MS OS 2.0 descriptor size must match its nested subsets");
_Static_assert(FTDI_USB_SERIAL_DESC_SIZE ==
                   (2U + (2U * (10U + FTDI_USB_BUILD_SERIAL_SUFFIX_LENGTH))),
               "FTDI serial descriptor size must match its generated suffix");
#if USB_CDC_ENABLED != 0U
_Static_assert(FTDI_USB_CDC_DATA_PACKET_SIZE == 16U,
               "CDC PMA allocation is sized for 16-byte bulk packets");
#endif
