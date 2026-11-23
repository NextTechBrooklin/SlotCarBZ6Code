/*******************************************************************************
  ATA8510 SPI Driver Local Definitions

  File Name:
    SYS_ATA851X_local.h

  Summary:
    SPI command IDs and timing constants for ATA8510.
*******************************************************************************/

#ifndef _SYS_ATA851X_LOCAL_H
#define _SYS_ATA851X_LOCAL_H

/* SPI timing constants (microseconds) */
#define SYS_ATA851X_T0 25   /* Time from NSS LOW to AVR active */
#define SYS_ATA851X_T1 18   /* Time from AVR active to first telegram byte */
#define SYS_ATA851X_T2 0    /* Time to shift in one SPI-byte with SCK */
#define SYS_ATA851X_T3 0    /* Pause between two bytes */
#define SYS_ATA851X_T4 35   /* Time for ATA8510 to handle last byte */
#define SYS_ATA851X_T5 16   /* SPI idle time between telegrams */

/* SPI buffer length */
#define SYS_ATA851X_BUFFER_LENGTH 32

/* SPI command IDs */
#define SYS_ATA851X_READ_FILL_LEVEL_RX_FIFO     (0x01)
#define SYS_ATA851X_READ_FILL_LEVEL_TX_FIFO     (0x02)
#define SYS_ATA851X_READ_FILL_LEVEL_RSSI_FIFO   (0x03)
#define SYS_ATA851X_GET_EVENT_BYTES             (0x04)
#define SYS_ATA851X_READ_RSSI_FIFO              (0x05)
#define SYS_ATA851X_READ_RX_FIFO                (0x06)
#define SYS_ATA851X_WRITE_SRAM                  (0x07)
#define SYS_ATA851X_READ_SRAM                   (0x08)
#define SYS_ATA851X_WRITE_EEPROM                (0x09)
#define SYS_ATA851X_READ_EEPROM                 (0x0A)
#define SYS_ATA851X_WRITE_TX_FIFO               (0x0B)
#define SYS_ATA851X_WRITE_TX_PREAMBLE_FIFO      (0x0C)
#define SYS_ATA851X_SET_SYSTEM_MODE             (0x0D)
#define SYS_ATA851X_CALIBRATE_AND_CHECK         (0x0E)
#define SYS_ATA851X_PATCH_SPI                   (0x0F)
#define SYS_ATA851X_SYSTEM_RESET_ROM            (0x10)
#define SYS_ATA851X_GET_VERSION_ROM             (0x12)
#define SYS_ATA851X_GET_VERSION_FLASH           (0x13)
#define SYS_ATA851X_CUST_CONF                   (0x14)
#define SYS_ATA851X_SYSTEM_RESET                (0x15)
#define SYS_ATA851X_TRIG_EEPROM_SECURE_WRITE    (0x16)
#define SYS_ATA851X_SET_VOLTAGE_MONITOR         (0x17)
#define SYS_ATA851X_OFF                         (0x18)
#define SYS_ATA851X_READ_TEMP_VALUE             (0x19)
#define SYS_ATA851X_INIT_SRAM_SERVICE           (0x1A)
#define SYS_ATA851X_START_RSSI_MEAS             (0x1B)
#define SYS_ATA851X_GET_RSSI_VALUE              (0x1C)
#define SYS_ATA851X_READ_RX_BUFFER_BYTE_INT     (0x1D)
#define SYS_ATA851X_READ_RSSI_BUFFER_BYTE_INT   (0x1E)

#endif /* _SYS_ATA851X_LOCAL_H */
