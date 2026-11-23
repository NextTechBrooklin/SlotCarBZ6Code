/*******************************************************************************
  ATA8510 SPI Driver Implementation

  File Name:
    SYS_ATA851X.c

  Summary:
    Full SPI command set for the ATA8510 UHF transceiver.
    Ported from SlotCarSam (SAM E51, SERCOM1 bare-metal) to the
    PIC32 BZ6 Curiosity Board (PIC32WM-BZ6204UE) under FreeRTOS.

  Description:
    Hardware mapping (BZ6 Curiosity, mikroBUS socket 2 / J905):
      SPI:     SERCOM4 host, blocking PLIB  (PE5=SCK PAD1, PA4=MISO PAD0,
                                             PA9=MOSI PAD3), 500 kHz, mode 0
      CS:      PB9  (active low)   - shared with SWDIO / SW801
      NRESET:  PC10 (active low)   - shared with green RGB LED
      NPWRON:  PB5  (active low)   - shared with BAT_MON
      IRQ:     PE3  (active low)   - shared with SW800 (read in ata8510.c)
    All pin / PLIB names are in board_hal.h (HAL_ATA_*).

    The command functions below are unchanged from the reference; only the
    transfer, GPIO and delay layer differs.
*******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "SYS_ATA851X.h"
#include "SYS_ATA851X_local.h"
#include "board_hal.h"
#include "os_port.h"

/* -------------------------------------------------------------------------- */
/* Hardware abstraction (BZ6)                                                  */
/* -------------------------------------------------------------------------- */

/* Full-duplex transfer: SERCOM4_SPI_WriteRead() in blocking (non-interrupt)
 * PLIB mode returns when the last byte has been shifted out/in. When
 * rxLen > txLen the PLIB clocks out dummy bytes, as the reference did. */
static void SYS_ATA851X_SpiTransfer(uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxLen)
{
    (void)HAL_ATA_SPI_WriteRead(tx, txLen, rx, rxLen);
}

#define SYS_ATA851X_TRANSFER(tx, txLen, rx, rxLen)     SYS_ATA851X_SpiTransfer((tx), (size_t)(txLen), (rx), (size_t)(rxLen))

#define SYS_ATA851X_CS_ENABLE      HAL_ATA_CS_Low()
#define SYS_ATA851X_CS_DISABLE     HAL_ATA_CS_High()
#define SYS_ATA851X_NRESET_CLEAR   HAL_ATA_NRESET_Low()
#define SYS_ATA851X_NRESET_SET     HAL_ATA_NRESET_High()
#define SYS_ATA851X_NPWRON1_CLEAR  HAL_ATA_NPWRON_Low()
#define SYS_ATA851X_NPWRON1_SET    HAL_ATA_NPWRON_High()

/* -------------------------------------------------------------------------- */

typedef struct {
    uint8_t system;
    uint8_t trx;
    uint8_t power;
    uint8_t config;
} SYS_ATA851X_EVENTS;

static SYS_ATA851X_EVENTS gSysAta851XEvents;
static uint8_t gSysAta851XTx[SYS_ATA851X_BUFFER_LENGTH];
static uint8_t gSysAta851XRx[SYS_ATA851X_BUFFER_LENGTH];

/* -------------------------------------------------------------------------- */
/* Delay helpers                                                               */
/* -------------------------------------------------------------------------- */

/* Millisecond delays yield to the RTOS (BLE task keeps running). */
void SYS_ATA851X_DelayMs(uint32_t ms)
{
    OS_DelayMs(ms);
}

/* Microsecond delays (SPI telegram timing T0..T5) busy-wait on the Cortex-M4
 * DWT cycle counter (core clock = configCPU_CLOCK_HZ from FreeRTOSConfig.h). */
void SYS_ATA851X_DelayUs(uint32_t us)
{
    static bool dwtReady = false;
    if (!dwtReady)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
        dwtReady = true;
    }

    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = us * (uint32_t)(configCPU_CLOCK_HZ / 1000000UL);   /* 128 MHz */
    while ((DWT->CYCCNT - start) < cycles)
    {
    }
}

/* -------------------------------------------------------------------------- */
/* Power-on sequence                                                           */
/* -------------------------------------------------------------------------- */

void SYS_ATA851X_PowerOn(void)
{
    /* SERCOM4 SPI is initialised by MCC (SYS_Initialize -> SERCOM4_SPI_Initialize).
     * GPIO directions/initial levels are also set by MCC; drive idle levels
     * again here so the sequence is self-contained. */
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_NRESET_SET;
    SYS_ATA851X_NPWRON1_SET;
    SYS_ATA851X_DelayUs(100);

    SYS_ATA851X_NPWRON1_CLEAR;
    SYS_ATA851X_NRESET_CLEAR;
    SYS_ATA851X_DelayUs(1000);
    SYS_ATA851X_NRESET_SET;
    SYS_ATA851X_DelayUs(5000);
}

/* -------------------------------------------------------------------------- */
/* SPI Commands                                                                */
/* -------------------------------------------------------------------------- */

uint8_t SYS_ATA851X_ReadFillLevelRxFifo(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_FILL_LEVEL_RX_FIFO;
    gSysAta851XTx[1] = gSysAta851XTx[2] = 0;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return gSysAta851XRx[2];
}

uint8_t SYS_ATA851X_ReadFillLevelTxFifo(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_FILL_LEVEL_TX_FIFO;
    gSysAta851XTx[1] = gSysAta851XTx[2] = 0;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return gSysAta851XRx[2];
}

uint8_t SYS_ATA851X_ReadFillLevelRssiFifo(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_FILL_LEVEL_RSSI_FIFO;
    gSysAta851XTx[1] = gSysAta851XTx[2] = 0;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return gSysAta851XRx[2];
}

void SYS_ATA851X_GetEventBytes(uint8_t *events)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_GET_EVENT_BYTES;
    memset(&gSysAta851XTx[1], 0x00, 0x03);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x04, gSysAta851XRx, 0x04);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    memcpy(events, gSysAta851XRx, 0x04);
}

void SYS_ATA851X_ReadRssiFifo(uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_RSSI_FIFO;
    gSysAta851XTx[1] = length;
    memset(&gSysAta851XTx[2], 0x00, length + 1);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 3, gSysAta851XRx, length + 3);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    memcpy(data, &gSysAta851XRx[3], length);
}

void SYS_ATA851X_ReadRxFifo(uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_RX_FIFO;
    gSysAta851XTx[1] = length;
    memset(&gSysAta851XTx[2], 0x00, length + 1);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 3, gSysAta851XRx, length + 3);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    memcpy(data, &gSysAta851XRx[3], length);
}

void SYS_ATA851X_WriteSramReg(uint16_t addr, uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_WRITE_SRAM;
    gSysAta851XTx[1] = length;
    gSysAta851XTx[2] = (addr >> 0x08) & 0xFF;
    gSysAta851XTx[3] = (addr & 0xFF);
    memcpy(&gSysAta851XTx[4], data, length);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 4, gSysAta851XRx, length + 4);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_ReadSramReg(uint16_t addr, uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_SRAM;
    gSysAta851XTx[1] = length;
    gSysAta851XTx[2] = (addr >> 0x08) & 0xFF;
    gSysAta851XTx[3] = (addr & 0xFF);
    memset(&gSysAta851XTx[4], 0x00, length + 1);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 5, gSysAta851XRx, length + 5);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    memcpy(data, &gSysAta851XRx[5], length);
}

void SYS_ATA851X_WriteEeprom(uint16_t addr, uint8_t data)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_WRITE_EEPROM;
    gSysAta851XTx[1] = (addr >> 0x08) & 0xFF;
    gSysAta851XTx[2] = (addr & 0xFF);
    gSysAta851XTx[3] = data;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x04, gSysAta851XRx, 0x04);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_WriteEepromBlock(uint16_t addr, uint8_t *data, uint8_t len)
{
    uint8_t i = 0;
    uint8_t tmpLen;
    while (i < len)
    {
        SYS_ATA851X_WriteSramReg(0x02E9, (uint8_t *)&addr + 1, 1);
        SYS_ATA851X_WriteSramReg(0x02EA, (uint8_t *)&addr, 1);
        if ((i + 7) <= len)
        {
            tmpLen = 7;
            SYS_ATA851X_WriteSramReg(0x02EB, &tmpLen, 1);
            SYS_ATA851X_WriteSramReg(0x02EC, &data[i], 7);
        }
        else
        {
            tmpLen = len - i;
            SYS_ATA851X_WriteSramReg(0x02EB, &tmpLen, 1);
            SYS_ATA851X_WriteSramReg(0x02EC, &data[i], len - i);
        }
        i += tmpLen;
        addr += tmpLen;
        SYS_ATA851X_TriggerEepromSecureWrite();
        SYS_ATA851X_DelayUs(10000 * tmpLen);
    }
}

uint8_t SYS_ATA851X_ReadEeprom(uint16_t addr)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_EEPROM;
    gSysAta851XTx[1] = (addr >> 0x08) & 0xFF;
    gSysAta851XTx[2] = (addr & 0xFF);
    gSysAta851XTx[3] = 0x00;
    gSysAta851XTx[4] = 0x00;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x05, gSysAta851XRx, 0x05);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return gSysAta851XRx[4];
}

void SYS_ATA851X_WriteTxFifo(uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_WRITE_TX_FIFO;
    gSysAta851XTx[1] = length;
    memcpy(&gSysAta851XTx[2], data, length);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 2, gSysAta851XRx, length + 2);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_WriteTxPreambleFifo(uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_WRITE_TX_PREAMBLE_FIFO;
    gSysAta851XTx[1] = length;
    memcpy(&gSysAta851XTx[2], data, length);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 2, gSysAta851XRx, length + 2);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_SetSystemMode(uint8_t system_mode_config, uint8_t service_channel_config)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_SET_SYSTEM_MODE;
    gSysAta851XTx[1] = system_mode_config;
    gSysAta851XTx[2] = service_channel_config;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_CalibrateAndCheck(uint8_t tune_check_config, uint8_t service_channel_config)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_CALIBRATE_AND_CHECK;
    gSysAta851XTx[1] = tune_check_config;
    gSysAta851XTx[2] = service_channel_config;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_PatchSpi(uint8_t parameter)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_PATCH_SPI;
    gSysAta851XTx[1] = parameter;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_SystemResetROM(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_SYSTEM_RESET_ROM;
    gSysAta851XTx[1] = 0x00;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
}

uint8_t SYS_ATA851X_GetVersionROM(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_GET_VERSION_ROM;
    gSysAta851XTx[1] = gSysAta851XTx[2] = 0;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return gSysAta851XRx[2];
}

uint8_t SYS_ATA851X_GetVersionFlash(uint8_t flash_version[], uint8_t *customer)
{
    uint8_t version = 0x00;
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_GET_VERSION_FLASH;
    memset(&gSysAta851XTx[1], 0x00, 0x05);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x06, gSysAta851XRx, 0x06);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    version = gSysAta851XRx[2];
    flash_version[0] = gSysAta851XRx[3];
    flash_version[1] = gSysAta851XRx[4];
    *customer = gSysAta851XRx[5];
    return version;
}

void SYS_ATA851X_CustomerConfCmd(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_CUST_CONF;
    gSysAta851XTx[1] = 0x00;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_SystemReset(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_SYSTEM_RESET;
    gSysAta851XTx[1] = 0x00;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
}

void SYS_ATA851X_TriggerEepromSecureWrite(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_TRIG_EEPROM_SECURE_WRITE;
    gSysAta851XTx[1] = 0xAA;
    gSysAta851XTx[2] = 0xCC;
    gSysAta851XTx[3] = 0xF0;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x04, gSysAta851XRx, 0x04);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_SetVoltageMonitor(uint8_t reg_vmcsr)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_SET_VOLTAGE_MONITOR;
    gSysAta851XTx[1] = reg_vmcsr;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_OffCommand(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_OFF;
    gSysAta851XTx[1] = 0;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
}

uint16_t SYS_ATA851X_ReadTemperatureValue(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_TEMP_VALUE;
    memset(&gSysAta851XTx[1], 0x00, 0x03);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x04, gSysAta851XRx, 0x04);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return (((uint16_t)gSysAta851XRx[2]) << 8) | (gSysAta851XRx[3]);
}

void SYS_ATA851X_InitSramService(uint8_t sram_service_nr, uint8_t eep_service_nr)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_INIT_SRAM_SERVICE;
    gSysAta851XTx[1] = sram_service_nr;
    gSysAta851XTx[2] = eep_service_nr;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x03, gSysAta851XRx, 0x03);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

void SYS_ATA851X_StartRssiMeas(uint8_t service_channel_config)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_START_RSSI_MEAS;
    gSysAta851XTx[1] = service_channel_config;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x02, gSysAta851XRx, 0x02);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
}

uint16_t SYS_ATA851X_GetRssiValue(void)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_GET_RSSI_VALUE;
    memset(&gSysAta851XTx[1], 0x00, 0x03);
    SYS_ATA851X_TRANSFER(gSysAta851XTx, 0x04, gSysAta851XRx, 0x04);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    return (((uint16_t)gSysAta851XRx[2]) << 8) | (gSysAta851XRx[3]);
}

void SYS_ATA851X_ReadRxFifoByteInt(uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_RX_BUFFER_BYTE_INT;
    memset(&gSysAta851XTx[1], 0x01, length - 1);
    gSysAta851XTx[length] = 0x00;
    gSysAta851XTx[length + 1] = 0x00;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 2, gSysAta851XRx, length + 2);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    memcpy(data, &gSysAta851XRx[2], length);
}

void SYS_ATA851X_ReadRssiFifoByteInt(uint8_t *data, uint8_t length)
{
    SYS_ATA851X_CS_ENABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T0);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T1);
    gSysAta851XTx[0] = SYS_ATA851X_READ_RSSI_BUFFER_BYTE_INT;
    memset(&gSysAta851XTx[1], 0x01, length - 1);
    gSysAta851XTx[length] = 0x00;
    gSysAta851XTx[length + 1] = 0x00;
    SYS_ATA851X_TRANSFER(gSysAta851XTx, length + 2, gSysAta851XRx, length + 2);
    SYS_ATA851X_DelayUs(SYS_ATA851X_T4);
    SYS_ATA851X_CS_DISABLE;
    SYS_ATA851X_DelayUs(SYS_ATA851X_T5);
    gSysAta851XEvents.system = gSysAta851XRx[0];
    gSysAta851XEvents.trx = gSysAta851XRx[1];
    memcpy(data, &gSysAta851XRx[2], length);
}
