#ifndef CH32_FT232_CDC_PORT_H
#define CH32_FT232_CDC_PORT_H

#include "byteStreamPort.h"

/* CDC 的 CH32/USB 细节止于这个适配器，转发服务只看到字节流 ops。 */
extern const ByteStreamPort UsbCdcPort0;

#endif /* CH32_FT232_CDC_PORT_H */
