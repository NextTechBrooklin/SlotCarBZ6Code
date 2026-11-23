#include "ghost.h"
#include "race.h"
#include "motorPWM.h"
#include "LEDs.h"
#include "ble_link.h"

#include <stdio.h>
#include <stdbool.h>

#include "board_hal.h"
#include "os_port.h"

/* Ghost mode runs the same race as Race_Run() — random PWM ramp, BLE lap/RPM
 * frames, lap counting to RACE_NUM_LAPS with per-car finish, the LED sequence,
 * and the return to idle monitoring — but as its own self-contained routine so
 * it does not depend on Race_Run(). The state below (car/ramp/PRNG/lap timer) is
 * private to this file and independent of the identical machinery in race.c.
 * Handoff back to idle monitoring reuses the public Race_IdleMonitorStart(). */

/* Ignore lap intervals shorter than this (sensor bounce / double-trigger).
 * The spurious crossing is discarded and timing continues from the last
 * valid crossing. */
#define MIN_LAP_TIME_S   (1.0f)

/* -------------------------------------------------------------------------
 * lap timer free-running lap timer
 * ---------------------------------------------------------------------- */
static volatile uint32_t g_lapOverflows = 0U;

__attribute__((unused)) static void Ghost_LapTmrCallback(TC_TIMER_STATUS status, uintptr_t context)
{
    (void)context;
    if ((status & TC_TIMER_STATUS_OVERFLOW) != 0U)
    {
        g_lapOverflows++;
    }
}

/* Returns a 32-bit tick snapshot: (overflow_count << 16) | counter_value. */
static uint32_t Ghost_GetTimestampTicks(void)
{
    uint32_t ovf = g_lapOverflows;
    uint16_t cnt = HAL_LAPTMR_CounterGet();
    return (ovf << 16) | (uint32_t)cnt;
}

/* -------------------------------------------------------------------------
 * BLE / UART transmit helper
 * ---------------------------------------------------------------------- */
static void Ghost_SendBlocking(const char *msg, size_t len)
{
    /* BZ6: queued to the BLE Transparent UART (APP task sends it) */
    BLE_Send(msg, len);
}

/* -------------------------------------------------------------------------
 * BLE frame senders
 *
 * Lap time : <C1,<seconds>\r\n  /  <C2,<seconds>\r\n
 * RPM      : <R1,<rpm>\r\n      /  <R2,<rpm>\r\n
 * ---------------------------------------------------------------------- */
static void Ghost_SendCarRpm(uint32_t carId, uint8_t pct)
{
    char msg[16];
    int  len = snprintf(msg, sizeof(msg), "<R%u,%u\r\n",
                        (unsigned)carId, (unsigned)pct);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        Ghost_SendBlocking(msg, (size_t)len);
    }
}

static void Ghost_SendRpm(uint8_t car1Pct, uint8_t car2Pct)
{
    Ghost_SendCarRpm(1U, car1Pct);
    Ghost_SendCarRpm(2U, car2Pct);
}

static void Ghost_SendLap(uint32_t car, float seconds)
{
    char msg[24];
    int  len = snprintf(msg, sizeof(msg), "<C%u,%.3f\r\n",
                        (unsigned)car, (double)seconds);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        Ghost_SendBlocking(msg, (size_t)len);
    }
}

/* -------------------------------------------------------------------------
 * Ghost-mode per-car state
 * ---------------------------------------------------------------------- */
typedef struct {
    volatile bool     lapReadyFlag;
    volatile float    lapTime;
    volatile uint32_t lapNumber;
    volatile uint32_t lapsComplete;
    volatile bool     skipFirst;
    volatile uint32_t lastTicks;
} CarState_t;

static CarState_t g_car1;
static CarState_t g_car2;

__attribute__((unused)) static void Ghost_Car1SensorCallback(uintptr_t context)
{
    (void)context;

    if (g_car1.skipFirst)
    {
        g_car1.skipFirst = false;
        g_car1.lastTicks = Ghost_GetTimestampTicks();
        return;
    }

    uint32_t now         = Ghost_GetTimestampTicks();
    uint32_t ticks       = now - g_car1.lastTicks;
    uint32_t freq        = HAL_LAPTMR_FrequencyGet();
    float    lapTime     = (freq > 0U) ? ((float)ticks / (float)freq) : 0.0f;

    /* Discard bounce / double-trigger: keep the previous baseline so the next
     * real crossing is timed from the last valid one. */
    if (lapTime < MIN_LAP_TIME_S)
    {
        return;
    }

    g_car1.lastTicks     = now;
    g_car1.lapTime       = lapTime;
    g_car1.lapsComplete++;
    g_car1.lapNumber     = g_car1.lapsComplete;
    g_car1.lapReadyFlag  = true;
}

__attribute__((unused)) static void Ghost_Car2SensorCallback(uintptr_t context)
{
    (void)context;

    if (g_car2.skipFirst)
    {
        g_car2.skipFirst = false;
        g_car2.lastTicks = Ghost_GetTimestampTicks();
        return;
    }

    uint32_t now         = Ghost_GetTimestampTicks();
    uint32_t ticks       = now - g_car2.lastTicks;
    uint32_t freq        = HAL_LAPTMR_FrequencyGet();
    float    lapTime     = (freq > 0U) ? ((float)ticks / (float)freq) : 0.0f;

    /* Discard bounce / double-trigger: keep the previous baseline so the next
     * real crossing is timed from the last valid one. */
    if (lapTime < MIN_LAP_TIME_S)
    {
        return;
    }

    g_car2.lastTicks     = now;
    g_car2.lapTime       = lapTime;
    g_car2.lapsComplete++;
    g_car2.lapNumber     = g_car2.lapsComplete;
    g_car2.lapReadyFlag  = true;
}

/* -------------------------------------------------------------------------
 * PRNG — LCG, seeded from lap timer counter at ghost start
 * ---------------------------------------------------------------------- */
#define PWM_MIN_PCT  (70U)
#define PWM_MAX_PCT  (85U)
#define PWM_STEP_PCT  (2U)

static uint32_t g_prngState = 0x5A17C3E1UL;

static uint32_t Ghost_PrngNext(void)
{
    g_prngState = (1664525UL * g_prngState) + 1013904223UL;
    return g_prngState;
}

static uint8_t Ghost_RandTarget(void)
{
    return (uint8_t)(PWM_MIN_PCT +
                     (Ghost_PrngNext() % (PWM_MAX_PCT - PWM_MIN_PCT + 1U)));
}

/* -------------------------------------------------------------------------
 * Non-blocking PWM ramp state (per car)
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  curPct;
    uint8_t  targetPct;
    bool     goingUp;
    uint32_t lastStepTicks;
    uint32_t stepIntervalTicks; /* ticks between 2% steps (~100 ms) */
} PwmRamp_t;

static PwmRamp_t g_ramp1;
static PwmRamp_t g_ramp2;

static void Ghost_RampInit(PwmRamp_t *r)
{
    r->curPct            = PWM_MIN_PCT;
    r->targetPct         = Ghost_RandTarget();
    r->goingUp           = true;
    /* BZ6 fix: ramp timed from the RTOS tick. The SAM build timed it from TC4,
     * which Ghost_Run() stops, so the ramp never advanced past PWM_MIN_PCT. */
    r->lastStepTicks     = OS_Millis();
    r->stepIntervalTicks = 100U; /* ms */
    if (r->stepIntervalTicks == 0U) { r->stepIntervalTicks = 1U; }
}

static void Ghost_RampNewLap(PwmRamp_t *r)
{
    r->curPct    = PWM_MIN_PCT;
    r->targetPct = Ghost_RandTarget();
    r->goingUp   = true;
}

static void Ghost_RampStep(PwmRamp_t *r)
{
    uint32_t now = OS_Millis();
    if ((now - r->lastStepTicks) < r->stepIntervalTicks)
    {
        return;
    }
    r->lastStepTicks = now;

    if (r->goingUp)
    {
        if ((uint32_t)r->curPct + PWM_STEP_PCT >= (uint32_t)r->targetPct)
        {
            r->curPct  = r->targetPct;
            r->goingUp = false;
        }
        else
        {
            r->curPct = (uint8_t)(r->curPct + PWM_STEP_PCT);
        }
    }
    else
    {
        if (r->curPct <= (uint8_t)(PWM_MIN_PCT + PWM_STEP_PCT))
        {
            r->curPct    = PWM_MIN_PCT;
            r->goingUp   = true;
            r->targetPct = Ghost_RandTarget();
        }
        else
        {
            r->curPct = (uint8_t)(r->curPct - PWM_STEP_PCT);
        }
    }
}

static void Ghost_UpdatePwm(bool car1Done, bool car2Done)
{
    uint8_t prev1 = g_ramp1.curPct;
    uint8_t prev2 = g_ramp2.curPct;

    /* A finished car is held at 0%, no longer ramped, and emits no more RPM
     * frames. Each still-racing car reports RPM only when its own duty
     * changes. */
    if (!car1Done) { Ghost_RampStep(&g_ramp1); }
    if (!car2Done) { Ghost_RampStep(&g_ramp2); }

    if (!car1Done && (g_ramp1.curPct != prev1))
    {
        MotorPWM_SetSpeed(MOTOR_CAR1, g_ramp1.curPct);
        Ghost_SendCarRpm(1U, g_ramp1.curPct);
    }
    if (!car2Done && (g_ramp2.curPct != prev2))
    {
        MotorPWM_SetSpeed(MOTOR_CAR2, g_ramp2.curPct);
        Ghost_SendCarRpm(2U, g_ramp2.curPct);
    }
}

/* -------------------------------------------------------------------------
 * Service pending ISR flags — ramp is reset when a lap completes
 * ---------------------------------------------------------------------- */
__attribute__((unused)) static void Ghost_ServiceCar(CarState_t *car, uint32_t carId, PwmRamp_t *ramp)
{
    if (car->lapReadyFlag)
    {
        float    t   = car->lapTime;
        uint32_t lap = car->lapNumber;
        car->lapReadyFlag = false;
        Ghost_SendLap(carId, t);
        printf("Ghost car %u lap %u: %.3f s\r\n",
               (unsigned)carId, (unsigned)lap, (double)t);
        Ghost_RampNewLap(ramp);
    }
}

/* -------------------------------------------------------------------------
 * Public ghost entry point — runs indefinitely until cancelled by command
 * ---------------------------------------------------------------------- */
void Ghost_Run(uint8_t speedPercent)
{
    if (speedPercent == 0U)
    {
        MotorPWM_SetSpeed(MOTOR_CAR1, 0U);
        MotorPWM_SetSpeed(MOTOR_CAR2, 0U);
        MotorPWM_Disable();
        LEDs_Clear();
        printf("Ghost: disabled\r\n");
        Race_IdleMonitorStart();
        return;
    }

    if (BLE_IsConnected())
    {
        printf("Ghost: BLE connected\r\n");
    }
    else
    {
        printf("Ghost: BLE not connected - outputs via UART only\r\n");
    }

    /* Disable lap sensors — ghost mode does not use them */
    HAL_EIC_Disable(HAL_EIC_LAP_CAR1);
    HAL_EIC_Disable(HAL_EIC_LAP_CAR2);
    HAL_LAPTMR_Stop();

    MotorPWM_Initialize();

    /* Seed PRNG then init ramps */
    g_prngState ^= OS_Millis();
    Ghost_RampInit(&g_ramp1);
    Ghost_RampInit(&g_ramp2);

    printf("Ghost: running (send GHOST:0 to stop)\r\n");

    MotorPWM_SetSpeed(MOTOR_CAR1, PWM_MIN_PCT);
    MotorPWM_SetSpeed(MOTOR_CAR2, PWM_MIN_PCT);
    Ghost_SendRpm(PWM_MIN_PCT, PWM_MIN_PCT);

    while (true)
    {
        /* Exit ghost mode on GHOST:0 */
        uint8_t ghostParam = 0xFFU;
        if (BLE_Link_ConsumeCommandWithParam("GHOST", &ghostParam))
        {
            if (ghostParam == 0U)
            {
                printf("Ghost: stop command received\r\n");
                break;
            }
        }

        if (!BLE_IsConnected())
        {
            printf("Ghost: BLE lost - stopping\r\n");
            break;
        }
        BLE_Link_DropPending();

        Ghost_UpdatePwm(false, false);

        /* Slow chasing LED pattern — advances every 200 ms */
        LEDs_ChaseStep(LEDS_COLOR_BLUE_DIM, 200U);

        BLE_Link_Kick();
        OS_DelayMs(5U);   /* yield to the BLE/APP task */
    }

    /* Zero PWM before releasing control */
    MotorPWM_SetSpeed(MOTOR_CAR1, 0U);
    MotorPWM_SetSpeed(MOTOR_CAR2, 0U);
    MotorPWM_Disable();

    LEDs_Clear();

    printf("Ghost: stopped\r\n");

    /* Return sensors to idle monitoring */
    Race_IdleMonitorStart();
}
