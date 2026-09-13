#ifndef CH32_FT232_JTAG_GOWIN_FLASH_H
#define CH32_FT232_JTAG_GOWIN_FLASH_H

#include <stdint.h>

#include "jtagManager.h"

/* Gowin GW1N/GW1NZ 内置 Flash 路径会用到的 IR。这里集中定义，避免
 * MPSSE 通用解析器继续靠“长零流”猜测当前事务。
 */
#define JTAG_GOWIN_IR_CONFIG_ENABLE (0x15U)
#define JTAG_GOWIN_IR_PROGRAM       (0x71U)
#define JTAG_GOWIN_IR_ERASE         (0x75U)

typedef enum
{
    JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH = 0U,
    JTAG_GOWIN_LONG_CLOCK_SUPPRESS,
    JTAG_GOWIN_LONG_CLOCK_ERASE
} JtagGowinLongClockAction;

uint8_t JtagGowinFlash_IsEraseWaitCandidate(const JTAGManager *self,
                                             uint8_t opcode,
                                             uint32_t byte_count);
JtagGowinLongClockAction JtagGowinFlash_LongClockByte(
    const JTAGManager *self, uint8_t data);
uint8_t JtagGowinFlash_CaptureProgramData(const JTAGManager *self,
                                          uint8_t data,
                                          uint8_t bits);
uint8_t JtagGowinFlash_IsProgramDr32Tail(const JTAGManager *self,
                                         uint8_t bits);
void JtagGowinFlash_ObserveInstruction(const JTAGManager *self,
                                       uint8_t data,
                                       uint8_t bits);

#endif /* CH32_FT232_JTAG_GOWIN_FLASH_H */
