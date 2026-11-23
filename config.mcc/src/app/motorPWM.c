#include "motorPWM.h"
#include "board_hal.h"

/*
 * TC8 is configured in 8-bit Compare / Normal PWM mode by MCC.
 * Period = 255 counts. MatchN sets the ON time: duty = matchN / 255.
 */

/* Maximum output duty cycle (0-100). Speed 0-100 maps to 0-this value. */
#define PWM_DUTY_SCALE_PCT  (75U)

static bool s_pwmStarted = false;

static uint8_t MotorPWM_PercentToCounts(uint8_t percent)
{
    if (percent >= MOTOR_PWM_SPEED_MAX)
    {
        percent = MOTOR_PWM_SPEED_MAX;
    }
    uint16_t scaled = ((uint16_t)percent * PWM_DUTY_SCALE_PCT) / MOTOR_PWM_SPEED_MAX;
    return (uint8_t)((scaled * HAL_PWM_PERIOD_COUNTS) / 100U);
}

void MotorPWM_Initialize(void)
{
    HAL_HB_IN1_Init();
    HAL_HB_IN4_Init();

    HAL_PWM_Car1Set(0U);
    HAL_PWM_Car2Set(0U);

    if (!s_pwmStarted)
    {
        /* TC8_CompareInitialize() is already called by SYS_Initialize() */
        HAL_PWM_Start();
        s_pwmStarted = true;
    }

    HAL_PWM_EN_Set();
}

void MotorPWM_Enable(void)
{
    HAL_PWM_EN_Set();
}

void MotorPWM_Disable(void)
{
    MotorPWM_Coast(MOTOR_CAR1);
    MotorPWM_Coast(MOTOR_CAR2);
    HAL_PWM_EN_Clear();
}

void MotorPWM_SetSpeed(MotorPWM_Car car, uint8_t percent)
{
    uint8_t counts = MotorPWM_PercentToCounts(percent);

    if (car == MOTOR_CAR1)
    {
        HAL_PWM_Car1Set(counts);
    }
    else
    {
        HAL_PWM_Car2Set(counts);
    }
}

void MotorPWM_Brake(MotorPWM_Car car)
{
    if (car == MOTOR_CAR1)
    {
        HAL_PWM_Car1Set(HAL_PWM_PERIOD_COUNTS);
    }
    else
    {
        HAL_PWM_Car2Set(HAL_PWM_PERIOD_COUNTS);
    }
}

void MotorPWM_Coast(MotorPWM_Car car)
{
    if (car == MOTOR_CAR1)
    {
        HAL_HB_IN1_Clear();
        HAL_PWM_Car1Set(0U);
    }
    else
    {
        HAL_HB_IN4_Clear();
        HAL_PWM_Car2Set(0U);
    }
}
