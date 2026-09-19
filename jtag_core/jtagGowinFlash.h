#ifndef CH32_FT232_JTAG_GOWIN_FLASH_H
#define CH32_FT232_JTAG_GOWIN_FLASH_H

#include <stdint.h>

#include "jtagManager.h"

/* Gowin GW1N/GW1NZ 内置 Flash 路径会用到的 IR。这里集中定义，避免
 * MPSSE 通用解析器继续靠“长零流”猜测当前事务。
 */
#define JTAG_GOWIN_IR_CONFIG_ENABLE (0x15U)
#define JTAG_GOWIN_IR_CONFIG_DISABLE (0x3AU)
#define JTAG_GOWIN_IR_SRAM_ERASE    (0x05U)
#define JTAG_GOWIN_IR_SRAM_DONE     (0x09U)
#define JTAG_GOWIN_IR_NOOP          (0x02U)
#define JTAG_GOWIN_IR_STATUS        (0x41U)
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
JtagGowinLongClockAction JtagGowinFlash_EraseTmsClock(
    const JTAGManager *self, uint8_t data, uint8_t bits);
uint8_t JtagGowinFlash_ProgramCommandValid(const JTAGManager *self,
                                           uint8_t opcode);
uint8_t JtagGowinFlash_ProgramDataValid(const JTAGManager *self,
                                        uint8_t data, uint8_t bits);
uint8_t JtagGowinFlash_CaptureProgramData(const JTAGManager *self,
                                          uint8_t data,
                                          uint8_t bits);
uint8_t JtagGowinFlash_IsProgramDr32Tail(const JTAGManager *self,
                                         uint8_t bits);
/* 暂存完整的两拍退出但不推进TAP；唯一的tap_state继续描述实际引脚状态。 */
uint8_t JtagGowinFlash_DeferProgramExit(const JTAGManager *self,
                                        uint8_t data, uint8_t bits);
uint8_t JtagGowinFlash_CanJoinProgramExit(const JTAGManager *self,
                                          uint8_t data);
/* 仅在调用者已预留输出预算、即将输出这两拍时消费，并推进既有TAP状态。 */
uint8_t JtagGowinFlash_TakeProgramExit(const JTAGManager *self);
void JtagGowinFlash_ObserveInstruction(const JTAGManager *self,
                                       uint8_t data,
                                       uint8_t bits);
/* Observe 在物理 GPIO 前推进 TAP；只能在对应移位已经输出后消费此动作。 */
uint8_t JtagGowinFlash_TakePrepare(const JTAGManager *self);

#endif /* CH32_FT232_JTAG_GOWIN_FLASH_H */
