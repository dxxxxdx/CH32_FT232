#ifndef CH32_FT232_FTDI_JTAG_SERVICE_H
#define CH32_FT232_FTDI_JTAG_SERVICE_H

#include <stdint.h>

#include "jtagManager.h"
#include "mpssePort.h"

typedef struct
{
    const MpssePort *port;
    const JTAGManager *jtag;
    uint32_t clock_budget;
} FtdiJtagServiceConfig;

typedef struct
{
    uint8_t seen_bus_reset;
    uint8_t seen_sio_reset;
    uint8_t seen_host_rx_purge;
    uint8_t seen_host_tx_purge;
    volatile uint8_t last_result;
    volatile uint8_t sticky_fault;
} FtdiJtagServiceState;

_Static_assert(sizeof(FtdiJtagServiceState) <= 6U,
               "FTDI JTAG bridge state exceeds its static RAM budget");

typedef struct
{
    const FtdiJtagServiceConfig *const config;
    FtdiJtagServiceState *const state;
} FtdiJtagService;

#define FTDI_JTAG_SERVICE_FLASH __attribute__((section(".rodata.ftdi_jtag")))

/* 组合根在文件作用域装配 port 与 JTAG 核。clock_budget 限制普通路径的单轮占用，
 * Gowin 页和擦除原子路径例外；它不参与 TCK 分频，主机 0x86 始终丢弃。
 */
#define FTDI_JTAG_SERVICE_DEFINE(name, port_object, jtag_object, budget)          \
    _Static_assert((budget) >= (MPSSE_PORT_RX_PACKET_SIZE * 8U),                  \
                   "JTAG service budget must cover one complete USB packet");  \
    static const FtdiJtagServiceConfig name##_config FTDI_JTAG_SERVICE_FLASH = { \
        .port = &(port_object),                                                   \
        .jtag = &(jtag_object),                                                   \
        .clock_budget = (budget)                                                  \
    };                                                                            \
    static FtdiJtagServiceState name##_state;                                     \
    static const FtdiJtagService name FTDI_JTAG_SERVICE_FLASH = {                 \
        .config = &name##_config,                                                  \
        .state = &name##_state                                                     \
    }

typedef enum
{
    FTDI_JTAG_SERVICE_IDLE = 0,
    FTDI_JTAG_SERVICE_PROGRESS,
    FTDI_JTAG_SERVICE_WAIT_CONFIGURATION,
    FTDI_JTAG_SERVICE_BACKPRESSURE,
    FTDI_JTAG_SERVICE_PORT_RX_LENGTH_FAULT,
    FTDI_JTAG_SERVICE_PORT_RX_OVERWRITE_FAULT,
    FTDI_JTAG_SERVICE_RX_TRANSFER_OVERFLOW,
    FTDI_JTAG_SERVICE_MPSSE_FAULT
} FtdiJtagServiceResult;

/* Init 必须在 GPIO 模式和初始电平配置完成之后、进入主循环之前调用。 */
void FtdiJtagService_Init(const FtdiJtagService *self);
FtdiJtagServiceResult FtdiJtagService_Service(const FtdiJtagService *self);

/* 默认静态实例由本模块装配，main 只保留启动和轮询顺序。 */
void FtdiJtagService0_Init(void);
void FtdiJtagService0_Poll(void);
FtdiJtagServiceResult FtdiJtagService0_LastResult(void);

#endif /* CH32_FT232_FTDI_JTAG_SERVICE_H */
