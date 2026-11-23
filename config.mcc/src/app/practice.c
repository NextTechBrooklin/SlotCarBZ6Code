#include "practice.h"
#include "motorPWM.h"
#include "LEDs.h"

static bool g_practiceActive = false;

void Practice_Run(uint8_t start)
{
    if (start != 0U)
    {
        g_practiceActive = true;
        MotorPWM_Initialize();
    }
    else
    {
        g_practiceActive = false;
        MotorPWM_Disable();
        LEDs_Clear();
    }
}

bool Practice_IsActive(void)
{
    return g_practiceActive;
}
