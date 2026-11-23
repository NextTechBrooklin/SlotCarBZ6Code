#ifndef RACE_H
#define RACE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RACE_NUM_LAPS   (3U)

void Race_IdleMonitorStart(void);
void Race_IdleMonitorService(void);
void Race_Run(void);

/* <R1,n>/<R2,n> RPM frames from the joystick magnitudes (race + practice).
 * force = true sends both lanes now; otherwise only changed lanes, <= 10 Hz. */
void Race_ReportRpm(bool force);
/* Send <Rn,0> for any lane whose last reported value was not 0. */
void Race_ReportRpmZero(void);

#ifdef __cplusplus
}
#endif

#endif /* RACE_H */
