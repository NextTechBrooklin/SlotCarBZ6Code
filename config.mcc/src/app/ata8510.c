/*******************************************************************************
  ATA8510 Application Source

  File Name:
    ata8510.c

  Summary:
    ATA8510 RF poll-master for SlotCar BZ6 (PIC32WM-BZ6204UE, FreeRTOS).
    Ported from SlotCarSam ata8510.c (SAM E51). Protocol is unchanged, so the
    existing SAMD21 joystick remotes work as-is.

  Description:
    Polls remote controllers A and B via UHF, collecting joystick data
    packets. Remote A drives car 1, remote B drives car 2; no reply or
    magnitude 0 coasts that car. Called from the RACE task (practice mode and
    Race_Run). Millisecond waits are vTaskDelay(), so one poll cycle (~45 ms
    worst case) does not block the BLE/APP task.

    ATA8510 Click in mikroBUS socket 2 (J905); pins in board_hal.h.
*******************************************************************************/

#include <string.h>
#include <stdio.h>
#include "ata8510.h"
#include "ata8510/SYS_ATA851X.h"
#include "motorPWM.h"
#include "board_hal.h"

/* Set to 0 to suppress ATA8510 debug prints */
#define ATA8510_DEBUG  1

#if ATA8510_DEBUG
#define ATA_PRINTF(...)  printf(__VA_ARGS__)
#else
#define ATA_PRINTF(...)  ((void)0)
#endif

/* -------------------------------------------------------------------------- */
/* ATA8510 IRQ pin — PE3 (mikroBUS 2 INT), active low                          */
/* -------------------------------------------------------------------------- */
#define ATA8510_IRQ_Get()  HAL_ATA_IRQ_Get()

/* -------------------------------------------------------------------------- */
/* RF protocol constants                                                       */
/* -------------------------------------------------------------------------- */
#define ATA8510_POLL_DATA       0xBB
#define ATA8510_TEST_DATA       0xAA
#define ATA8510_TX_MODE         0x31
#define ATA8510_RX_MODE         0x32
#define ATA8510_TX_SERVICE      0x41    /* Path A, Service 1 */
#define ATA8510_POLL_TIMEOUT_MS 15
#define ATA8510_POLL_PACKET_LEN 12

/* -------------------------------------------------------------------------- */
/* Module data                                                                 */
/* -------------------------------------------------------------------------- */

static uint8_t gEvent[4];
static uint8_t gRxBuffer[32];
static uint8_t gRxLen;

static uint8_t gPollTxBuffer[ATA8510_POLL_PACKET_LEN];
static uint8_t gPollCounter = 0;
static uint32_t gMsgCount = 0;

/* Latest joystick magnitude per car (0-100) and per-car enable.
 * Index 0 = car 1 / remote 'A', index 1 = car 2 / remote 'B'.
 * Written by the RACE task only (PollCycle), read by the RPM reporter. */
static uint8_t gMag[2]        = { 0U, 0U };
static bool    gCarEnabled[2] = { true, true };

/* Apply one remote's magnitude to its car. A disabled car (finished its
 * race) is always coasted and reported as 0. */
static void ATA8510_ApplyCar(uint8_t idx, uint8_t magnitude)
{
    MotorPWM_Car motor = (idx == 0U) ? MOTOR_CAR1 : MOTOR_CAR2;

    if (!gCarEnabled[idx])
    {
        magnitude = 0U;
    }
    gMag[idx] = magnitude;

    if (magnitude > 0U)
    {
        MotorPWM_SetSpeed(motor, magnitude);
    }
    else
    {
        MotorPWM_Coast(motor);
    }
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

static uint8_t ATA8510_Checksum(const uint8_t data[], uint8_t len)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        sum += data[i];
    }
    return (uint8_t)(0xFFU - sum + 1U);
}

/* -------------------------------------------------------------------------- */
/* Poll cycle internals                                                        */
/* -------------------------------------------------------------------------- */

static void ATA8510_SendPoll(uint8_t remoteId)
{
    uint8_t i;
    for (i = 0; i < 6; i++)
    {
        gPollTxBuffer[i] = 0xFF;  /* preamble */
    }
    gPollTxBuffer[6]  = 0xFE;              /* start byte */
    gPollTxBuffer[7]  = ATA8510_POLL_DATA;  /* poll type */
    gPollTxBuffer[8]  = 0x00;              /* unused */
    gPollTxBuffer[9]  = remoteId;          /* which remote to respond */
    gPollTxBuffer[10] = gPollCounter;      /* counter */
    gPollTxBuffer[11] = ATA8510_Checksum(&gPollTxBuffer[7], 4);

    /* Idle -> load FIFO -> TX */
    SYS_ATA851X_SetSystemMode(0x00, 0x00);
    SYS_ATA851X_WriteTxFifo(gPollTxBuffer, ATA8510_POLL_PACKET_LEN);
    SYS_ATA851X_SetSystemMode(ATA8510_TX_MODE, ATA8510_TX_SERVICE);

    /* Wait for TX to complete (~5ms at 20kbps) */
    SYS_ATA851X_DelayMs(7);

    gPollCounter++;
}

static bool ATA8510_WaitResponse(void)
{
    uint32_t elapsed = 0;

    /* Switch to RX mode — flush stale events first */
    SYS_ATA851X_SetSystemMode(0x00, 0x00);
    SYS_ATA851X_GetEventBytes(gEvent);
    SYS_ATA851X_SetSystemMode(ATA8510_RX_MODE, ATA8510_TX_SERVICE);

    /* Poll IRQ pin with 1ms delays */
    while (elapsed < ATA8510_POLL_TIMEOUT_MS)
    {
        if (ATA8510_IRQ_Get() == 0U)
        {
            /* IRQ fired — wait for packet to finish (12 bytes @ 20kbps ~4.8ms) */
            SYS_ATA851X_DelayMs(6);

            /* Read events */
            SYS_ATA851X_GetEventBytes(gEvent);

            if (((gEvent[1] & 0x70) == 0x70) && (gEvent[3] & 0x40))
            {
                /* Valid packet — read data */
                gRxLen = SYS_ATA851X_ReadFillLevelRxFifo();
                if (gRxLen > (uint8_t)(sizeof(gRxBuffer) - 3U))
                {
                    /* driver frame buffer is 32 bytes incl. 3 header bytes */
                    gRxLen = (uint8_t)(sizeof(gRxBuffer) - 3U);
                }
                SYS_ATA851X_ReadRxFifo(gRxBuffer, gRxLen);

                SYS_ATA851X_SetSystemMode(0x00, 0x00);
                return true;
            }
            /* Invalid event */
            SYS_ATA851X_SetSystemMode(0x00, 0x00);
            return false;
        }
        SYS_ATA851X_DelayMs(1);
        elapsed++;
    }

    /* Timeout */
    SYS_ATA851X_SetSystemMode(0x00, 0x00);
    return false;
}

static void ATA8510_ProcessResponse(void)
{
    if (gRxLen == 0) return;

    /* Checksum validation */
    if (gRxBuffer[gRxLen - 1U] != ATA8510_Checksum(gRxBuffer, (uint8_t)(gRxLen - 1U)))
    {
        ATA_PRINTF("ATA8510: BAD CSUM len=%u\r\n", gRxLen);
        return;
    }

    /* Check for joystick data packet:
     * rxBuffer[0] = 0xAA (RF_JOYSTICK_DATA)
     * rxBuffer[1] = magnitude (0-100)
     * rxBuffer[2] = remote ID ('A' or 'B')
     * rxBuffer[3] = counter
     * rxBuffer[4] = checksum
     */
    if (gRxBuffer[0] == ATA8510_TEST_DATA)
    {
        gMsgCount++;

        uint8_t magnitude = gRxBuffer[1];
        uint8_t remoteId  = gRxBuffer[2];
        uint8_t counter   = gRxBuffer[3];

        ATA_PRINTF("ATA8510: %c M:%u C:%u\r\n",
               remoteId, magnitude, counter);

        /* Clamp to 0-100 */
        if (magnitude > 100U)
        {
            magnitude = 100U;
        }

        /* Map remote A -> Car 1, remote B -> Car 2 */
        if (remoteId == 'A')
        {
            ATA8510_ApplyCar(0U, magnitude);
        }
        else if (remoteId == 'B')
        {
            ATA8510_ApplyCar(1U, magnitude);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

void ATA8510_Init(void)
{
    /* CS (PB9), NRESET (PC10), NPWRON (PB5) and IRQ (PE3, pull-up) are
     * configured by MCC (Pin Configuration) - see MCC_Setup_BZ6.md. */
    HAL_ATA_GPIO_Init();

    ATA_PRINTF("ATA8510: power on\r\n");
    SYS_ATA851X_PowerOn();

    /* System reset to clear any stuck state */
    SYS_ATA851X_SystemReset();
    SYS_ATA851X_DelayMs(10);

    /* Read and clear stale events multiple times */
    SYS_ATA851X_GetEventBytes(gEvent);
    ATA_PRINTF("ATA8510: events[1]: %02X %02X %02X %02X\r\n",
           gEvent[0], gEvent[1], gEvent[2], gEvent[3]);
    SYS_ATA851X_DelayMs(1);
    SYS_ATA851X_GetEventBytes(gEvent);
    ATA_PRINTF("ATA8510: events[2]: %02X %02X %02X %02X\r\n",
           gEvent[0], gEvent[1], gEvent[2], gEvent[3]);

    /* Check IRQ pin state after clearing events */
    ATA_PRINTF("ATA8510: IRQ pin = %u (expect 1=inactive)\r\n", (unsigned)ATA8510_IRQ_Get());

    /* Read ROM version to verify SPI communication */
    uint8_t romVer = SYS_ATA851X_GetVersionROM();
    ATA_PRINTF("ATA8510: ROM version: 0x%02X\r\n", romVer);

    /* Read Flash version (for ATA8515 flash-based modules) */
    uint8_t flashVer[2] = {0};
    uint8_t customer = 0;
    uint8_t fv = SYS_ATA851X_GetVersionFlash(flashVer, &customer);
    ATA_PRINTF("ATA8510: Flash ver: 0x%02X, cust: 0x%02X, fw: %02X %02X\r\n",
           fv, customer, flashVer[0], flashVer[1]);

    /* Set idle */
    SYS_ATA851X_SetSystemMode(0x00, 0x00);
    SYS_ATA851X_DelayUs(200);

    ATA_PRINTF("ATA8510: poll master active\r\n");
}

static uint32_t gPollCycleCount = 0;

void ATA8510_PollCycle(void)
{
    gPollCycleCount++;

    /* Print status every 100 cycles (~5 sec) */
    if ((gPollCycleCount % 100U) == 1U)
    {
        ATA_PRINTF("ATA8510: poll #%u, msgs=%u, IRQ=%u\r\n",
               (unsigned)gPollCycleCount, (unsigned)gMsgCount,
               (unsigned)ATA8510_IRQ_Get());
    }
    /* On first cycle, print TX events for diagnostic */
    if (gPollCycleCount == 1U)
    {
        ATA8510_SendPoll('A');
        SYS_ATA851X_GetEventBytes(gEvent);
        ATA_PRINTF("ATA8510: TX events: %02X %02X %02X %02X\r\n",
               gEvent[0], gEvent[1], gEvent[2], gEvent[3]);
        SYS_ATA851X_SetSystemMode(0x00, 0x00);
        SYS_ATA851X_DelayMs(2);
        return;
    }
    /* Poll Remote A */
    ATA8510_SendPoll('A');
    if (ATA8510_WaitResponse())
    {
        ATA8510_ProcessResponse();
    }
    else
    {
        /* No response from Remote A — coast car 1 */
        ATA8510_ApplyCar(0U, 0U);
    }

    /* Poll Remote B */
    SYS_ATA851X_DelayMs(2);
    ATA8510_SendPoll('B');
    if (ATA8510_WaitResponse())
    {
        ATA8510_ProcessResponse();
    }
    else
    {
        /* No response from Remote B — coast car 2 */
        ATA8510_ApplyCar(1U, 0U);
    }
}

uint32_t ATA8510_GetMsgCount(void)
{
    return gMsgCount;
}

uint8_t ATA8510_GetMagnitude(uint8_t car)
{
    return ((car >= 1U) && (car <= 2U)) ? gMag[car - 1U] : 0U;
}

void ATA8510_SetCarEnabled(uint8_t car, bool enabled)
{
    if ((car >= 1U) && (car <= 2U))
    {
        gCarEnabled[car - 1U] = enabled;
        if (!enabled)
        {
            ATA8510_ApplyCar((uint8_t)(car - 1U), 0U);
        }
    }
}

void ATA8510_ResetState(void)
{
    gMag[0] = 0U;
    gMag[1] = 0U;
    gCarEnabled[0] = true;
    gCarEnabled[1] = true;
}
