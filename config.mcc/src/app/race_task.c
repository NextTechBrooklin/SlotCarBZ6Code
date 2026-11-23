/*******************************************************************************
  race_task.c  -  SlotCar application task (replaces the SAM E51 main.c loop)

  Runs at a lower priority than the BLE/APP task. All original "blocking"
  routines (Race_Run, Ghost_Run, RaceDemo_Run, LED sequences) run here
  unchanged in structure - their delays are vTaskDelay() via os_port.h.
 *******************************************************************************/
#include "race_task.h"

#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board_hal.h"
#include "os_port.h"
#include "ble_link.h"
#include "ata8510.h"
#include "raceDemo.h"
#include "race.h"
#include "ghost.h"
#include "practice.h"
#include "LEDs.h"
#include "motorPWM.h"

#define RACE_TASK_STACK_WORDS  (1024U)
#define RACE_TASK_PRIORITY     (tskIDLE_PRIORITY + 1U)
#define RACE_TASK_PERIOD_MS    (10U)

static void RaceTask(void *arg)
{
    (void)arg;

    HAL_LED0_Init();

    printf("SlotCarBZ6: LED colour cycle test\r\n");
    LEDs_Initialize();
    LEDs_ColorCycleTest(2U);

    /* Motors off until a mode enables them */
    MotorPWM_Initialize();
    MotorPWM_Disable();

    printf("SlotCarBZ6: ATA8510 init\r\n");
    ATA8510_Init();

    Race_IdleMonitorStart();
    printf("SlotCarBZ6: ready - advertising as SLOTCAR_MASTERS\r\n");

    bool prevConnected = false;

    for (;;)
    {
        BLE_Link_Service();

        /* Practice: the 433 MHz joystick remotes drive the cars */
        if (Practice_IsActive())
        {
            ATA8510_PollCycle();
            Race_ReportRpm(false);
        }

        Race_IdleMonitorService();

        /* ---- BLE commands from phone app ---- */
        if (BLE_Link_ConsumeCommand("START_RACE"))
        {
            printf("SlotCarBZ6: START_RACE\r\n");
            if (Practice_IsActive())
            {
                /* protocol.md 13: START_RACE leaves practice mode */
                Practice_Run(0U);
                Race_ReportRpmZero();
            }
            Race_Run();
            printf("SlotCarBZ6: race complete\r\n");

            /* A command that ended the race early is still pending - start
             * the next pass so it is dispatched instead of dropped below. */
            continue;
        }

        uint8_t ghostSpeed = 0U;
        if (BLE_Link_ConsumeCommandWithParam("GHOST", &ghostSpeed))
        {
            printf("SlotCarBZ6: GHOST:%u\r\n", ghostSpeed);
            Ghost_Run(ghostSpeed);
        }

        uint8_t practiceParam = 0U;
        if (BLE_Link_ConsumeCommandWithParam("PRACTICE", &practiceParam))
        {
            printf("SlotCarBZ6: PRACTICE:%u\r\n", practiceParam);
            bool wasActive = Practice_IsActive();
            Practice_Run(practiceParam);
            ATA8510_ResetState();
            if (practiceParam != 0U)
            {
                Race_ReportRpm(true);       /* gauges start in sync at 0 */
            }
            else if (wasActive)
            {
                Race_ReportRpmZero();       /* RPM stream ends with 0 */
            }
        }

        /* DEMO replaces button SW800: PE3 is now the ATA8510 IRQ line */
        if (BLE_Link_ConsumeCommand("DEMO"))
        {
            printf("SlotCarBZ6: DEMO - starting demo\r\n");
            RaceDemo_Run();
        }

        (void)BLE_Link_ConsumeCommand("STOP_RACE");   /* nothing running */
        BLE_Link_DropPending();

        if (Practice_IsActive())
        {
            LEDs_ChaseStep(LEDS_COLOR_YELLOW_DIM, 200U);
        }

        /* ---- Connection indicator ---- */
        bool connected = BLE_IsConnected();
        if (connected != prevConnected)
        {
            if (connected)
            {
                HAL_LED0_On();
            }
            else
            {
                HAL_LED0_Off();
            }
            prevConnected = connected;
        }

        BLE_Link_Kick();
        OS_DelayMs(RACE_TASK_PERIOD_MS);
    }
}

void RaceTask_Create(void)
{
    (void)xTaskCreate(RaceTask, "RACE", RACE_TASK_STACK_WORDS, NULL,
                      RACE_TASK_PRIORITY, NULL);
}
