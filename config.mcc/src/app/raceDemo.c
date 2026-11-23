#include "raceDemo.h"
#include "motorPWM.h"
#include "LEDs.h"

#include <stdio.h>

#include "ble_link.h"
#include "os_port.h"

/* -------------------------------------------------------------------------
 * PRNG
 * ---------------------------------------------------------------------- */
static uint32_t g_prngState = 0x5A17C3E1UL;

static uint32_t RaceDemo_PrngNext(void)
{
    g_prngState = (1664525UL * g_prngState) + 1013904223UL;
    return g_prngState;
}

/* -------------------------------------------------------------------------
 * BLE transmit helpers
 * ---------------------------------------------------------------------- */
static void RaceDemo_SendBlocking(const char *message, size_t messageLength)
{
    /* BZ6: queued to the BLE Transparent UART (APP task sends it) */
    BLE_Send(message, messageLength);
}

static void RaceDemo_SendRpm(uint32_t r1, uint32_t r2)
{
    char msg[16];
    int  len;

    len = snprintf(msg, sizeof(msg), "<R1,%u\r\n", (unsigned)r1);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        RaceDemo_SendBlocking(msg, (size_t)len);
    }

    len = snprintf(msg, sizeof(msg), "<R2,%u\r\n", (unsigned)r2);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        RaceDemo_SendBlocking(msg, (size_t)len);
    }
}

/* -------------------------------------------------------------------------
 * Demo step — set both cars to random duty cycles in [50, 60]% and dwell
 * for 10 seconds, sending an RPM packet every 500 ms during the dwell.
 * ---------------------------------------------------------------------- */
#define DEMO_SPEED_MIN    (50U)
#define DEMO_SPEED_RANGE  (11U)   /* 50..60 inclusive */
#define DEMO_DWELL_MS     (10000U)
#define DEMO_RPM_INTERVAL (500U)
#define DEMO_RPM_TICKS    (DEMO_DWELL_MS / DEMO_RPM_INTERVAL)  /* 20 */

static bool RaceDemo_ShouldStop(void)
{
    if (!BLE_IsStreamOpen())
    {
        printf("RaceDemo: BLE stream closed - stopping\r\n");
        return true;
    }
    if (BLE_Link_ConsumeCommand("STOP_RACE"))
    {
        printf("RaceDemo: STOP_RACE\r\n");
        return true;
    }
    BLE_Link_DropPending();
    return false;
}

static bool RaceDemo_RunStep(uint32_t stepNumber)
{
    uint8_t  speed1 = (uint8_t)(DEMO_SPEED_MIN + (RaceDemo_PrngNext() % DEMO_SPEED_RANGE));
    uint8_t  speed2 = (uint8_t)(DEMO_SPEED_MIN + (RaceDemo_PrngNext() % DEMO_SPEED_RANGE));
    uint32_t tick;

    printf("RaceDemo: step %u — car1 %u%% car2 %u%%\r\n",
           (unsigned)stepNumber, (unsigned)speed1, (unsigned)speed2);

    MotorPWM_SetSpeed(MOTOR_CAR1, speed1);
    MotorPWM_SetSpeed(MOTOR_CAR2, speed2);

    for (tick = 0U; tick < DEMO_RPM_TICKS; tick++)
    {
        RaceDemo_SendRpm((uint32_t)speed1, (uint32_t)speed2);
        OS_DelayMs(DEMO_RPM_INTERVAL);

        /* BZ6: the demo used to run forever - now it can be stopped */
        if (RaceDemo_ShouldStop())
        {
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Public entry point — cycles random steps until STOP_RACE or BLE stream closes
 * ---------------------------------------------------------------------- */
void RaceDemo_Run(void)
{
    uint32_t stepIndex = 0U;

    g_prngState ^= OS_Millis();

    if (BLE_IsStreamOpen() == false)
    {
        printf("RaceDemo: BLE stream not open, aborting\r\n");
        return;
    }

    MotorPWM_Initialize();

    printf("RaceDemo: starting demo (50-60%% random steps, 10 s dwell)\r\n");

    LEDs_RaceStart();

    LEDs_SetAll(LEDS_COLOR_GREEN_DIM);
    LEDs_Show();

    while (RaceDemo_RunStep(++stepIndex))
    {
    }

    MotorPWM_Disable();
    LEDs_Clear();
    printf("RaceDemo: stopped\r\n");
}
