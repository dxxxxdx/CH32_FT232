#ifndef CH32_FT232_USB_CONF_H
#define CH32_FT232_USB_CONF_H

#include "UartForwardConfig.h"

#if USB_CDC_ENABLED != 0U
/* EP0、FT2232 A/B 四个端点以及 CDC ACM 三个端点占满 EP0~EP7。 */
#define EP_NUM              (8U)
#else
/* 关闭 UART 时恢复纯 FT2232 拓扑，只发布 EP0~EP4。 */
#define EP_NUM              (5U)
#endif

/* PMA 地址按 USB 外设的逻辑字节地址填写。0x00~0x3F 留给八个端点的 BTABLE。
 * FTDI 必须维持 64 字节包；CDC 采用 Full Speed bulk 合法的 16 字节包，
 * 把全部端点控制块和缓冲区压进片内 512 字节 PMA。
 */
#define BTABLE_ADDRESS      (0x0000U)
#define ENDP0_RXADDR        (0x0040U)
#define ENDP0_TXADDR        (0x0080U)
#define ENDP1_TXADDR        (0x00C0U)
#define ENDP2_RXADDR        (0x0100U)
#define ENDP3_TXADDR        (0x0140U)
#define ENDP4_RXADDR        (0x0180U)
#if USB_CDC_ENABLED != 0U
#define ENDP5_TXADDR        (0x01C0U)
#define ENDP6_RXADDR        (0x01D0U)
#define ENDP7_TXADDR        (0x01E0U)
#endif

/* Bulk 采用传输完成事件推进，不开 SOF/ESOF；当前数据面不靠 1 ms 时基轮询。 */
#define IMR_MSK (CNTR_CTRM | CNTR_RESETM)

#endif /* CH32_FT232_USB_CONF_H */
