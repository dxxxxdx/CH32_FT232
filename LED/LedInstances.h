#ifndef CH32_FT232_LED_INSTANCES_H
#define CH32_FT232_LED_INSTANCES_H

#include "Led.h"
#include "LedInstancesConfig.h"

#if RUN_LED_ENABLED != 0U
extern const Led RunLed0;
#endif

void LedInstances_Init(void);
void LedInstances_Service(void);

#endif /* CH32_FT232_LED_INSTANCES_H */
