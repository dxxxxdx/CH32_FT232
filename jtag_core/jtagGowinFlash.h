#ifndef CH32_FT232_JTAG_GOWIN_FLASH_H
#define CH32_FT232_JTAG_GOWIN_FLASH_H

#include "jtagManager.h"

/* Gowin 内核独占控制用 TAP/DR32 状态；诊断 TAP 仅观察，不能控制 GPIO。 */
typedef enum
{
    JTAG_GOWIN_LONG_CLOCK_PASSTHROUGH = 0U,
    JTAG_GOWIN_LONG_CLOCK_SUPPRESS,
    JTAG_GOWIN_LONG_CLOCK_ERASE
} JtagGowinLongClockAction;

typedef enum
{
    JTAG_GOWIN_DR32_PASSTHROUGH = 0U,
    JTAG_GOWIN_DR32_ENTER_CAPTURED,
    JTAG_GOWIN_DR32_CAPTURED,
    JTAG_GOWIN_DR32_TAIL_CAPTURED,
    JTAG_GOWIN_DR32_COMMIT,
    JTAG_GOWIN_DR32_INVALID
} JtagGowinDr32Action;

void JtagGowinFlash_Reset(const JTAGManager *self);
uint8_t JtagGowinFlash_IsEraseWaitCandidate(uint8_t opcode, uint32_t byte_count);
uint8_t JtagGowinFlash_BeginCommand(const JTAGManager *self, uint8_t opcode);
void JtagGowinFlash_ProgramWordDone(const JTAGManager *self);
uint32_t JtagGowinFlash_PrepareClocks(const JTAGManager *self, uint8_t data, uint16_t bits);
uint8_t JtagGowinFlash_SuppressIdle(const JTAGManager *self, uint8_t data, uint16_t bits);
/* 已确认COMMIT时调用；0表示擦除等待，其它值为编程Idle拍数。 */
uint32_t JtagGowinFlash_PendingIdleClocks(const JTAGManager *self);
JtagGowinLongClockAction JtagGowinFlash_LongClockByte(
    const JTAGManager *self, uint8_t data);
JtagGowinDr32Action JtagGowinFlash_CaptureProgramData(const JTAGManager *self,
                                        uint8_t data, uint16_t bits);
void JtagGowinFlash_ObserveInstruction(const JTAGManager *self,
                                     uint8_t data, uint16_t bits);

#endif /* CH32_FT232_JTAG_GOWIN_FLASH_H */
