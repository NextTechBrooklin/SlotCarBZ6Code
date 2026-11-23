#ifndef MOTORPWM_H
#define MOTORPWM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * H-Bridge 4 Click (TB67H450FNG) motor driver  -  BZ6 port
 *
 * Car 1 - TC8 WO1 -> IN2 (PA15, mikroBUS1 RST) + HB_IN1 GPIO (PB10, AN)
 * Car 2 - TC8 WO0 -> IN3 (PE4,  mikroBUS1 INT) + HB_IN4 GPIO (PD1,  PWM)
 * Enable - PWM_EN GPIO (PA10, mikroBUS1 CS)
 * (SAM E51 used TC0 WO0 + TC1 WO1; on BZ6 one TC in 8-bit NPWM mode gives
 *  both channels with a shared period. PLIB names are in board_hal.h.)
 *
 * TB67H450FNG truth table (IN1/IN4 held low permanently):
 *   IN1=L  IN2=PWM  -> Drive, speed proportional to duty
 *   IN1=L  IN2=L    -> Coast (free-wheel)
 *
 * Speed is expressed as 0-100 (percent).
 */

#define MOTOR_PWM_SPEED_MAX   (100U)

typedef enum
{
    MOTOR_CAR1 = 0,
    MOTOR_CAR2 = 1
} MotorPWM_Car;

void MotorPWM_Initialize(void);
void MotorPWM_Enable(void);
void MotorPWM_Disable(void);
void MotorPWM_SetSpeed(MotorPWM_Car car, uint8_t percent);
void MotorPWM_Brake(MotorPWM_Car car);
void MotorPWM_Coast(MotorPWM_Car car);

#endif /* MOTORPWM_H */
