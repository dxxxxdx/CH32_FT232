#ifndef CH32_FT232_USB_CONF_H
#define CH32_FT232_USB_CONF_H

/* EP0，加上 FT2232 A/B 两组 Bulk IN/OUT。端点号连续到 EP4，因此库表为 5。 */
#define EP_NUM              (5U)

/* PMA 地址按 USB 外设的逻辑字节地址填写。0x00~0x27 留给五个端点的 BTABLE。 */
#define BTABLE_ADDRESS      (0x0000U)
#define ENDP0_RXADDR        (0x0040U)
#define ENDP0_TXADDR        (0x0080U)
#define ENDP1_TXADDR        (0x00C0U)
#define ENDP2_RXADDR        (0x0100U)
#define ENDP3_TXADDR        (0x0140U)
#define ENDP4_RXADDR        (0x0180U)

/* Bulk 采用传输完成事件推进，不开 SOF/ESOF；当前数据面不靠 1 ms 时基轮询。 */
#define IMR_MSK (CNTR_CTRM | CNTR_RESETM)

#endif /* CH32_FT232_USB_CONF_H */
