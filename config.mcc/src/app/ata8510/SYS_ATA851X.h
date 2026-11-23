/*******************************************************************************
  ATA8510 SPI Driver Header

  File Name:
    SYS_ATA851X.h

  Summary:
    SPI command interface for the ATA8510 UHF transceiver.
*******************************************************************************/

#ifndef _SYS_ATA851X_H
#define _SYS_ATA851X_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void SYS_ATA851X_PowerOn(void);
uint8_t SYS_ATA851X_ReadFillLevelRxFifo(void);
uint8_t SYS_ATA851X_ReadFillLevelTxFifo(void);
uint8_t SYS_ATA851X_ReadFillLevelRssiFifo(void);
void SYS_ATA851X_GetEventBytes(uint8_t *events);
void SYS_ATA851X_ReadRssiFifo(uint8_t *data, uint8_t length);
void SYS_ATA851X_ReadRxFifo(uint8_t *data, uint8_t length);
void SYS_ATA851X_WriteSramReg(uint16_t addr, uint8_t *data, uint8_t length);
void SYS_ATA851X_ReadSramReg(uint16_t addr, uint8_t *data, uint8_t length);
void SYS_ATA851X_WriteEeprom(uint16_t addr, uint8_t data);
void SYS_ATA851X_WriteEepromBlock(uint16_t addr, uint8_t *data, uint8_t len);
uint8_t SYS_ATA851X_ReadEeprom(uint16_t addr);
void SYS_ATA851X_WriteTxFifo(uint8_t *data, uint8_t length);
void SYS_ATA851X_WriteTxPreambleFifo(uint8_t *data, uint8_t length);
void SYS_ATA851X_SetSystemMode(uint8_t system_mode_config, uint8_t service_channel_config);
void SYS_ATA851X_CalibrateAndCheck(uint8_t tune_check_config, uint8_t service_channel_config);
void SYS_ATA851X_PatchSpi(uint8_t parameter);
void SYS_ATA851X_SystemResetROM(void);
uint8_t SYS_ATA851X_GetVersionROM(void);
uint8_t SYS_ATA851X_GetVersionFlash(uint8_t flash_version[], uint8_t *customer);
void SYS_ATA851X_CustomerConfCmd(void);
void SYS_ATA851X_SystemReset(void);
void SYS_ATA851X_TriggerEepromSecureWrite(void);
void SYS_ATA851X_SetVoltageMonitor(uint8_t reg_vmcsr);
void SYS_ATA851X_OffCommand(void);
uint16_t SYS_ATA851X_ReadTemperatureValue(void);
void SYS_ATA851X_InitSramService(uint8_t sram_service_nr, uint8_t eep_service_nr);
void SYS_ATA851X_StartRssiMeas(uint8_t service_channel_config);
uint16_t SYS_ATA851X_GetRssiValue(void);
void SYS_ATA851X_ReadRxFifoByteInt(uint8_t *data, uint8_t length);
void SYS_ATA851X_ReadRssiFifoByteInt(uint8_t *data, uint8_t length);

void SYS_ATA851X_DelayMs(uint32_t ms);
void SYS_ATA851X_DelayUs(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ATA851X_H */
