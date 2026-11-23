/*******************************************************************************
  os_port.h  -  replaces SAM E51 SYSTICK_DelayMs / SYSTICK_GetMillis

  The BZ6 BLE stack runs on FreeRTOS, and SysTick belongs to the RTOS. All
  blocking delays in the original code become vTaskDelay(), so the BLE/APP
  task keeps running while the race task "blocks".
  Requires configTICK_RATE_HZ = 1000 (MCC FreeRTOS setting).
 *******************************************************************************/
#ifndef OS_PORT_H
#define OS_PORT_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

static inline void OS_DelayMs(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay((t == 0U) ? 1U : t);
}

static inline uint32_t OS_Millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

#endif /* OS_PORT_H */
