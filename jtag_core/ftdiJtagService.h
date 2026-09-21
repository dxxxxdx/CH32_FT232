#ifndef CH32_FT232_FTDI_JTAG_SERVICE_H
#define CH32_FT232_FTDI_JTAG_SERVICE_H

#include <stdint.h>

#include "jtagManager.h"
#include "mpssePort.h"

typedef struct
{
    const MpssePort *port;
    const JTAGManager *jtag;
} FtdiJtagServiceConfig;

typedef struct
{
    uint8_t seen_bus_reset;
    uint8_t seen_sio_reset;
    uint8_t seen_host_rx_purge;
    uint8_t seen_host_tx_purge;
    volatile uint8_t last_result;
    volatile uint8_t sticky_fault;
    /* 批次调度只归service所有；长度/内容/解析状态仍只存于manager。 */
    uint8_t rx_phase;
} FtdiJtagServiceState;

_Static_assert(sizeof(FtdiJtagServiceState) <= 12U,
               "FTDI JTAG bridge state exceeds its static RAM budget");

typedef struct
{
    const FtdiJtagServiceConfig *const config;
    FtdiJtagServiceState *const state;
} FtdiJtagService;

#define FTDI_JTAG_SERVICE_FLASH __attribute__((section(".rodata.ftdi_jtag")))

/* 批次由 service 唯一调度；解析器不再通过时钟预算切断一批 GPIO 输出。 */
#define FTDI_JTAG_SERVICE_DEFINE(name, port_object, jtag_object)                \
    static const FtdiJtagServiceConfig name##_config FTDI_JTAG_SERVICE_FLASH = { \
        .port = &(port_object),                                                \
        .jtag = &(jtag_object)                                                 \
    };                                                                        \
    static FtdiJtagServiceState name##_state;                                   \
    static const FtdiJtagService name FTDI_JTAG_SERVICE_FLASH = {                \
        .config = &name##_config,                                              \
        .state = &name##_state                                                 \
    }

typedef enum
{
    FTDI_JTAG_SERVICE_IDLE = 0,
    FTDI_JTAG_SERVICE_PROGRESS,
    FTDI_JTAG_SERVICE_WAIT_CONFIGURATION,
    FTDI_JTAG_SERVICE_BACKPRESSURE,
    FTDI_JTAG_SERVICE_PORT_RX_LENGTH_FAULT,
    FTDI_JTAG_SERVICE_PORT_RX_OVERWRITE_FAULT,
    FTDI_JTAG_SERVICE_TX_OVERFLOW,
    FTDI_JTAG_SERVICE_SEQUENCE_FAULT
} FtdiJtagServiceResult;

/* Init 必须在 GPIO 模式和初始电平配置完成之后、进入主循环之前调用。 */
void FtdiJtagService_Init(const FtdiJtagService *self);
FtdiJtagServiceResult FtdiJtagService_Service(const FtdiJtagService *self);

/* 默认静态实例由本模块装配，main 只保留启动和轮询顺序。 */
void FtdiJtagService0_Init(void);
void FtdiJtagService0_Poll(void);
FtdiJtagServiceResult FtdiJtagService0_LastResult(void);

#endif /* CH32_FT232_FTDI_JTAG_SERVICE_H */
