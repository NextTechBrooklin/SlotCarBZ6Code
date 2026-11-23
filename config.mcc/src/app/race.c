#include "race.h"
#include "motorPWM.h"
#include "LEDs.h"
#include "ble_link.h"
#include "ata8510.h"

#include <stdio.h>
#include <stdbool.h>

#include "board_hal.h"
#include "os_port.h"

/* -------------------------------------------------------------------------
 * lap timer free-running lap timer
 * ---------------------------------------------------------------------- */
static volatile uint32_t g_lapOverflows = 0U;

static void Race_LapTmrCallback(TC_TIMER_STATUS status, uintptr_t context)
{
    (void)context;
    if ((status & TC_TIMER_STATUS_OVERFLOW) != 0U)
    {
        g_lapOverflows++;
    }
}

/* Returns elapsed ticks since timer start.
 * lap timer overflows at CC[0] (MPWM period), not at 65536. */
static uint32_t Race_GetTimestampTicks(void)
{
    uint32_t ovf = g_lapOverflows;
    uint16_t cnt = HAL_LAPTMR_CounterGet();
    uint16_t per = HAL_LAPTMR_PeriodGet();
    return (ovf * (uint32_t)per) + (uint32_t)cnt;
}

/* -------------------------------------------------------------------------
 * BLE / UART transmit helper
 * ---------------------------------------------------------------------- */
static void Race_SendBlocking(const char *msg, size_t len)
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
static void Race_SendRpm(uint8_t car1Pct, uint8_t car2Pct)
{
    char msg[16];
    int  len;

    len = snprintf(msg, sizeof(msg), "<R1,%u\r\n", (unsigned)car1Pct);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        Race_SendBlocking(msg, (size_t)len);
    }

    len = snprintf(msg, sizeof(msg), "<R2,%u\r\n", (unsigned)car2Pct);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        Race_SendBlocking(msg, (size_t)len);
    }
}

static void Race_SendLap(uint32_t car, float seconds)
{
    char msg[24];
    int  len = snprintf(msg, sizeof(msg), "<C%u,%.3f\r\n",
                        (unsigned)car, (double)seconds);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        Race_SendBlocking(msg, (size_t)len);
    }
}

/* -------------------------------------------------------------------------
 * RPM reporter (protocol.md 7.1 / 9): <Rn,0-100> from the joystick magnitude,
 * only when a lane's value changes, at most every RACE_RPM_MIN_INTERVAL_MS per
 * lane (~10 Hz), ending with 0 when the car stops or the mode ends.
 * ---------------------------------------------------------------------- */
#define RACE_RPM_MIN_INTERVAL_MS  (100U)

static uint8_t  g_rpmSent[2]   = { 0U, 0U };
static uint32_t g_rpmSentMs[2] = { 0U, 0U };

static void Race_SendRpmLane(uint8_t lane, uint8_t pct)
{
    char msg[12];
    int  len = snprintf(msg, sizeof(msg), "<R%u,%u\r\n", (unsigned)lane, (unsigned)pct);
    if ((len > 0) && ((size_t)len < sizeof(msg)))
    {
        Race_SendBlocking(msg, (size_t)len);
    }
    g_rpmSent[lane - 1U]   = pct;
    g_rpmSentMs[lane - 1U] = OS_Millis();
}

void Race_ReportRpm(bool force)
{
    uint32_t now = OS_Millis();
    uint8_t  lane;

    for (lane = 1U; lane <= 2U; lane++)
    {
        uint8_t pct = ATA8510_GetMagnitude(lane);
        bool    due = (now - g_rpmSentMs[lane - 1U]) >= RACE_RPM_MIN_INTERVAL_MS;

        if (force || ((pct != g_rpmSent[lane - 1U]) && due))
        {
            Race_SendRpmLane(lane, pct);
        }
    }
}

void Race_ReportRpmZero(void)
{
    uint8_t lane;

    for (lane = 1U; lane <= 2U; lane++)
    {
        if (g_rpmSent[lane - 1U] != 0U)
        {
            Race_SendRpmLane(lane, 0U);
        }
    }
}

/* -------------------------------------------------------------------------
 * Idle-mode per-sensor state (used outside of a race)
 * ---------------------------------------------------------------------- */
typedef struct {
    volatile bool     lapReadyFlag;
    volatile float    lapTime;
    volatile uint32_t lapCount;
    volatile bool     hasBaseline;
    volatile uint32_t lastTicks;
} IdleSensor_t;

static IdleSensor_t g_idle1;
static IdleSensor_t g_idle2;

static void Race_IdleSensorTrigger(IdleSensor_t *s)
{
    uint32_t now = Race_GetTimestampTicks();

    if (!s->hasBaseline)
    {
        s->hasBaseline = true;
        s->lastTicks   = now;
        return;
    }

    uint32_t ticks  = now - s->lastTicks;
    uint32_t freq   = HAL_LAPTMR_FrequencyGet();

    /* Ignore triggers faster than 1 second (wheel gap noise) */
    if (ticks < freq)
    {
        return;
    }

    s->lastTicks    = now;
    s->lapTime      = (freq > 0U) ? ((float)ticks / (float)freq) : 0.0f;
    s->lapCount++;
    s->lapReadyFlag = true;
}

static void Race_IdleSensor1Callback(uintptr_t context)
{
    (void)context;
    Race_IdleSensorTrigger(&g_idle1);
}

static void Race_IdleSensor2Callback(uintptr_t context)
{
    (void)context;
    Race_IdleSensorTrigger(&g_idle2);
}

static void Race_ServiceIdleSensor(IdleSensor_t *s, uint32_t carId)
{
    if (s->lapReadyFlag)
    {
        float t = s->lapTime;
        s->lapReadyFlag = false;
        /* Console only: the PC app treats a <Cn> outside a race as a race
         * start (protocol.md 8), so idle laps are not sent over BLE. */
        printf("Idle car %u lap %u: %.2f s (idle, not sent)\r\n",
               (unsigned)carId, (unsigned)s->lapCount, (double)t);
    }
}

/* -------------------------------------------------------------------------
 * Public idle-monitor API
 * ---------------------------------------------------------------------- */
void Race_IdleMonitorStart(void)
{
    HAL_EIC_Initialize();
    HAL_LAPTMR_Initialize();

    g_idle1 = (IdleSensor_t){ false, 0.0f, 0U, false, 0U };
    g_idle2 = (IdleSensor_t){ false, 0.0f, 0U, false, 0U };

    HAL_EIC_CallbackRegister(HAL_EIC_LAP_CAR1, Race_IdleSensor1Callback, 0U);
    HAL_EIC_CallbackRegister(HAL_EIC_LAP_CAR2, Race_IdleSensor2Callback, 0U);

    g_lapOverflows = 0U;
    HAL_LAPTMR_CounterSet(0U);
    HAL_LAPTMR_CallbackRegister(Race_LapTmrCallback, 0U);
    HAL_LAPTMR_Start();

    HAL_EIC_Enable(HAL_EIC_LAP_CAR1);
    HAL_EIC_Enable(HAL_EIC_LAP_CAR2);
}

void Race_IdleMonitorService(void)
{
    Race_ServiceIdleSensor(&g_idle1, 1U);
    Race_ServiceIdleSensor(&g_idle2, 2U);
}

/* -------------------------------------------------------------------------
 * Race-mode per-car state
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

/* Ticks at green light — edges within 3 seconds of this are blanked */
static volatile uint32_t g_raceStartTicks = 0U;
#define RACE_START_BLANKING_S  (2U)

static void Race_Car1SensorCallback(uintptr_t context)
{
    (void)context;

    /* Finished: at most RACE_NUM_LAPS <C1> frames per race (protocol.md 8) */
    if (g_car1.lapsComplete >= RACE_NUM_LAPS)
    {
        return;
    }

    uint32_t now         = Race_GetTimestampTicks();
    uint32_t freq        = HAL_LAPTMR_FrequencyGet();

    /* Blank all edges in the first 3 seconds after green */
    if ((now - g_raceStartTicks) < (freq * RACE_START_BLANKING_S))
    {
        return;
    }

    uint32_t ticks       = now - g_car1.lastTicks;

    /* Ignore triggers faster than 1 second (wheel gap noise) */
    if (ticks < freq)
    {
        return;
    }

    g_car1.lastTicks     = now;
    g_car1.lapTime       = (freq > 0U) ? ((float)ticks / (float)freq) : 0.0f;
    g_car1.lapsComplete++;
    g_car1.lapNumber     = g_car1.lapsComplete;
    g_car1.lapReadyFlag  = true;
}

static void Race_Car2SensorCallback(uintptr_t context)
{
    (void)context;

    /* Finished: at most RACE_NUM_LAPS <C2> frames per race (protocol.md 8) */
    if (g_car2.lapsComplete >= RACE_NUM_LAPS)
    {
        return;
    }

    uint32_t now         = Race_GetTimestampTicks();
    uint32_t freq        = HAL_LAPTMR_FrequencyGet();

    /* Blank all edges in the first 3 seconds after green */
    if ((now - g_raceStartTicks) < (freq * RACE_START_BLANKING_S))
    {
        return;
    }

    uint32_t ticks       = now - g_car2.lastTicks;

    /* Ignore triggers faster than 1 second (wheel gap noise) */
    if (ticks < freq)
    {
        return;
    }

    g_car2.lastTicks     = now;
    g_car2.lapTime       = (freq > 0U) ? ((float)ticks / (float)freq) : 0.0f;
    g_car2.lapsComplete++;
    g_car2.lapNumber     = g_car2.lapsComplete;
    g_car2.lapReadyFlag  = true;
}

/* -------------------------------------------------------------------------
 * PRNG — LCG, seeded from lap timer counter at race start
 * ---------------------------------------------------------------------- */
#define PWM_MIN_PCT  (20U)
#define PWM_MAX_PCT  (50U)
#define PWM_STEP_PCT  (2U)

static uint32_t g_prngState = 0x5A17C3E1UL;

static uint32_t Race_PrngNext(void)
{
    g_prngState = (1664525UL * g_prngState) + 1013904223UL;
    return g_prngState;
}

static uint8_t Race_RandTarget(void)
{
    return (uint8_t)(PWM_MIN_PCT +
                     (Race_PrngNext() % (PWM_MAX_PCT - PWM_MIN_PCT + 1U)));
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

static void Race_RampInit(PwmRamp_t *r)
{
    r->curPct            = PWM_MIN_PCT;
    r->targetPct         = Race_RandTarget();
    r->goingUp           = true;
    r->lastStepTicks     = Race_GetTimestampTicks();
    r->stepIntervalTicks = HAL_LAPTMR_FrequencyGet() / 10U; /* 100 ms */
    if (r->stepIntervalTicks == 0U) { r->stepIntervalTicks = 1U; }
}

static void Race_RampNewLap(PwmRamp_t *r)
{
    r->curPct    = PWM_MIN_PCT;
    r->targetPct = Race_RandTarget();
    r->goingUp   = true;
}

static void Race_RampStep(PwmRamp_t *r)
{
    uint32_t now = Race_GetTimestampTicks();
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
            r->targetPct = Race_RandTarget();
        }
        else
        {
            r->curPct = (uint8_t)(r->curPct - PWM_STEP_PCT);
        }
    }
}

__attribute__((unused)) static void Race_UpdatePwm(void)
{
    uint8_t prev1 = g_ramp1.curPct;
    uint8_t prev2 = g_ramp2.curPct;

    Race_RampStep(&g_ramp1);
    Race_RampStep(&g_ramp2);

    if ((g_ramp1.curPct != prev1) || (g_ramp2.curPct != prev2))
    {
        MotorPWM_SetSpeed(MOTOR_CAR1, g_ramp1.curPct);
        MotorPWM_SetSpeed(MOTOR_CAR2, g_ramp2.curPct);
        Race_SendRpm(g_ramp1.curPct, g_ramp2.curPct);
    }
}

/* -------------------------------------------------------------------------
 * Service pending ISR flags — ramp is reset when a lap completes
 * ---------------------------------------------------------------------- */
static void Race_ServiceCar(CarState_t *car, uint32_t carId, PwmRamp_t *ramp)
{
    if (car->lapReadyFlag)
    {
        float    t   = car->lapTime;
        uint32_t lap = car->lapNumber;
        car->lapReadyFlag = false;
        Race_SendLap(carId, t);
        printf("Car %u lap %u: %.3f s\r\n",
               (unsigned)carId, (unsigned)lap, (double)t);
        Race_RampNewLap(ramp);
    }
}

/* -------------------------------------------------------------------------
 * Public race entry point
 * ---------------------------------------------------------------------- */
void Race_Run(void)
{
    if (BLE_IsConnected())
    {
        printf("Race: BLE connected\r\n");
    }
    else
    {
        printf("Race: BLE not connected - outputs via UART only\r\n");
    }

    /* Take over sensors from idle monitor */
    HAL_EIC_Disable(HAL_EIC_LAP_CAR1);
    HAL_EIC_Disable(HAL_EIC_LAP_CAR2);
    HAL_LAPTMR_Stop();

    g_car1 = (CarState_t){ false, 0.0f, 0U, 0U, false, 0U };
    g_car2 = (CarState_t){ false, 0.0f, 0U, 0U, false, 0U };

    HAL_EIC_CallbackRegister(HAL_EIC_LAP_CAR1, Race_Car1SensorCallback, 0U);
    HAL_EIC_CallbackRegister(HAL_EIC_LAP_CAR2, Race_Car2SensorCallback, 0U);

    g_lapOverflows = 0U;
    HAL_LAPTMR_CounterSet(0U);
    HAL_LAPTMR_CallbackRegister(Race_LapTmrCallback, 0U);
    HAL_LAPTMR_Start();

    /* Seed PRNG from lap timer counter then init ramps */
    g_prngState ^= (uint32_t)HAL_LAPTMR_CounterGet();
    Race_RampInit(&g_ramp1);
    Race_RampInit(&g_ramp2);

    printf("Race: starting %u-lap race\r\n", (unsigned)RACE_NUM_LAPS);

    /* The PC app has already run the start-light countdown and sends
     * START_RACE at lights-out, so go green immediately (LEDs_RaceStart()
     * is no longer used) and confirm with the CSV "START" frame. */
    LEDs_SetAll(LEDS_COLOR_GREEN_DIM);
    LEDs_Show();
    Race_SendBlocking("START\r\n", 7U);

    ATA8510_ResetState();
    Race_ReportRpm(true);       /* both gauges start at 0 */

    MotorPWM_Initialize();

    /* Start lap timing from green light — the 3s blanking rejects early edges */
    uint32_t startTicks  = Race_GetTimestampTicks();
    g_raceStartTicks     = startTicks;
    g_car1.lastTicks     = startTicks;
    g_car2.lastTicks     = startTicks;

    HAL_EIC_Enable(HAL_EIC_LAP_CAR1);
    HAL_EIC_Enable(HAL_EIC_LAP_CAR2);

    bool car1Finished = false;
    bool car2Finished = false;

    while ((g_car1.lapsComplete < RACE_NUM_LAPS) ||
           (g_car2.lapsComplete < RACE_NUM_LAPS))
    {
        /* 433 MHz joystick remotes: A -> car 1, B -> car 2 (yields ~45 ms) */
        ATA8510_PollCycle();
        Race_ServiceCar(&g_car1, 1U, &g_ramp1);
        Race_ServiceCar(&g_car2, 2U, &g_ramp2);

        /* A car that has done its laps is coasted and ignores its remote */
        if (!car1Finished && (g_car1.lapsComplete >= RACE_NUM_LAPS))
        {
            car1Finished = true;
            ATA8510_SetCarEnabled(1U, false);
            printf("Race: car 1 finished\r\n");
        }
        if (!car2Finished && (g_car2.lapsComplete >= RACE_NUM_LAPS))
        {
            car2Finished = true;
            ATA8510_SetCarEnabled(2U, false);
            printf("Race: car 2 finished\r\n");
        }

        Race_ReportRpm(false);

        /* BZ6: abort on STOP_RACE. A BLE disconnect does not stop the race -
         * the RF remotes keep driving and lap times still go to the console. */
        if (BLE_Link_ConsumeCommand("STOP_RACE"))
        {
            printf("Race: aborted\r\n");
            break;
        }

        /* Any other command (GHOST:n, PRACTICE:n, START_RACE, DEMO) means the
         * PC app has left this race - e.g. Reset / Restart, which send nothing
         * themselves. End the race and leave the command pending so the race
         * task executes it (START_RACE then simply starts a new race). */
        if (BLE_Link_HasPending())
        {
            printf("Race: aborted by '%s'\r\n", BLE_Link_PendingText());
            break;
        }
        BLE_Link_Kick();
        OS_DelayMs(5U);   /* yield to the BLE/APP task */
    }

    HAL_EIC_Disable(HAL_EIC_LAP_CAR1);
    HAL_EIC_Disable(HAL_EIC_LAP_CAR2);
    HAL_LAPTMR_Stop();

    MotorPWM_Disable();
    ATA8510_ResetState();
    Race_ReportRpmZero();       /* protocol.md 9: RPM stream ends with 0 */

    LEDs_SetAll(LEDS_COLOR_BLUE_DIM);
    LEDs_Show();

    printf("Race: complete\r\n");

    /* Return sensors to idle monitoring */
    Race_IdleMonitorStart();
}
