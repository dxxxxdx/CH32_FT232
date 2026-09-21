#ifndef CH32_FT232_BOARD_GPIO_H
#define CH32_FT232_BOARD_GPIO_H

/* 板级 GPIO 驱动接口；配置入口统一为 BoardConfig.h。 */
typedef struct BoardGpio BoardGpio;
extern const BoardGpio BoardGpio0;

void BoardGpio_Init(const BoardGpio *self);
void BoardGpio_UsbdPinsRelease(const BoardGpio *self);
void BoardGpio_UsbdPinsDriveLow(const BoardGpio *self);

#endif /* CH32_FT232_BOARD_GPIO_H */
