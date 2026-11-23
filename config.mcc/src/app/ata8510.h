/*******************************************************************************
  ATA8510 Application Header

  File Name:
    ata8510.h

  Summary:
    ATA8510 RF poll-master interface for SlotCar BZ6 (SAMD21 joystick remotes).
*******************************************************************************/

#ifndef _ATA8510_H
#define _ATA8510_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ATA8510_Init(void);
void ATA8510_PollCycle(void);
uint32_t ATA8510_GetMsgCount(void);

/* Latest joystick magnitude for car 1 or 2 (0-100); 0 if the remote did not
 * answer the last poll or the car is disabled. Used for the <Rn,> RPM frames. */
uint8_t ATA8510_GetMagnitude(uint8_t car);

/* Disable a car (e.g. it finished the race): its motor is coasted and further
 * joystick input for it is ignored until re-enabled or ATA8510_ResetState(). */
void ATA8510_SetCarEnabled(uint8_t car, bool enabled);

/* Magnitudes -> 0, both cars enabled. Call at the start/end of a mode. */
void ATA8510_ResetState(void);

#ifdef __cplusplus
}
#endif

#endif /* _ATA8510_H */
