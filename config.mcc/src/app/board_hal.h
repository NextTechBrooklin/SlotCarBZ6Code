/*******************************************************************************
  board_hal.h  -  SlotCar BZ6 hardware abstraction

  Target : PIC32 BZ6 Curiosity Board (EV31U42A), PIC32WM-BZ6204UE module
  Origin : ported from SlotCarSam (ATSAME51J20A)

  EVERY MCC-generated peripheral name used by the application lives in this one
  file. If you choose different TC / SERCOM instances or GPIO custom names in
  MCC, change them HERE only - no other application file touches PLIB names.

  Required MCC setup (see MCC_Setup_BZ6.md):
    TC8      8-bit, Compare, Normal PWM (NPWM), PER = 255, WO0 + WO1 enabled
             WO1 -> PA15 -> Car 1 PWM (H-Bridge IN2, mikroBUS1 RST)
             WO0 -> PE4  -> Car 2 PWM (H-Bridge IN3, mikroBUS1 INT)
    TC2      16-bit Timer, period ~50 ms, period/overflow interrupt enabled
    SERCOM4  SPI Host, 500 kHz, mode 0, RX enabled, BLOCKING (no interrupt)
             -> ATA8510 Click in mikroBUS 2 (J905)
             SCK = PAD1 (PE5), MISO = PAD0 (PA4), MOSI = PAD3 (PA9)
    SERCOM1  SPI Host, ~4 MHz, DO = PAD0 (PA7, XPRO J900-11) -> WS2812 data
    SERCOM0  USART 115200, PA5 TX / PA6 RX      -> MCP2200 VCOM (STDIO/printf)
    EIC      two EXTINT channels, rising edge, filter on -> lap sensors
    GPIO     LED0 (PB7), HB_IN1 (PB10), HB_IN4 (PD1), PWM_EN (PA10)
             ATA_CS (PB9, out, high)      - mikroBUS2 CS  (shares SWDIO/SW801)
             ATA_NRESET (PC10, out, high) - mikroBUS2 RST (shares green LED)
             ATA_NPWRON (PC11, out, high) - mikroBUS2 PWM (shares RMII_EN)
             ATA_IRQ (PE3, in, pull-up)   - mikroBUS2 INT (shares SW800)
 *******************************************************************************/
#ifndef BOARD_HAL_H
#define BOARD_HAL_H

#include "definitions.h"   /* MCC: pulls in every generated PLIB header */

/* -------------------------------------------------------------------------
 * Motor PWM  (TC8, 8-bit compare, NPWM)
 * Car 1 = IN2 = PA15 = TC8_WO1 (Match1);  Car 2 = IN3 = PE4 = TC8_WO0 (Match0)
 * ---------------------------------------------------------------------- */
#define HAL_PWM_PERIOD_COUNTS          (255U)
#define HAL_PWM_Start()                TC8_CompareStart()
#define HAL_PWM_Car1Set(cnt)           ((void)TC8_Compare8bitMatch1Set((uint8_t)(cnt)))
#define HAL_PWM_Car2Set(cnt)           ((void)TC8_Compare8bitMatch0Set((uint8_t)(cnt)))

/* H-Bridge 4 Click direction / enable GPIOs (MCC custom pin names)
 *   HB_IN1 = PB10 (mikroBUS1 AN), HB_IN4 = PD1 (mikroBUS1 PWM),
 *   PWM_EN = PA10 (mikroBUS1 CS)                                          */
#define HAL_HB_IN1_Init()              do { HB_IN1_OutputEnable(); HB_IN1_Clear(); } while (0)
#define HAL_HB_IN4_Init()              do { HB_IN4_OutputEnable(); HB_IN4_Clear(); } while (0)
#define HAL_HB_IN1_Clear()             HB_IN1_Clear()
#define HAL_HB_IN4_Clear()             HB_IN4_Clear()
#define HAL_PWM_EN_Set()               do { PWM_EN_OutputEnable(); PWM_EN_Set(); } while (0)
#define HAL_PWM_EN_Clear()             PWM_EN_Clear()

/* -------------------------------------------------------------------------
 * Lap timer  (TC2, 16-bit timer, period interrupt)
 * ---------------------------------------------------------------------- */
#define HAL_LAPTMR_Initialize()        TC2_TimerInitialize()
#define HAL_LAPTMR_Start()             TC2_TimerStart()
#define HAL_LAPTMR_Stop()              TC2_TimerStop()
#define HAL_LAPTMR_CounterSet(v)       TC2_Timer16bitCounterSet((uint16_t)(v))
#define HAL_LAPTMR_CounterGet()        TC2_Timer16bitCounterGet()
#define HAL_LAPTMR_PeriodGet()         TC2_Timer16bitPeriodGet()
#define HAL_LAPTMR_FrequencyGet()      TC2_TimerFrequencyGet()
#define HAL_LAPTMR_CallbackRegister(cb, ctx) TC2_TimerCallbackRegister((cb), (ctx))

/* -------------------------------------------------------------------------
 * Lap sensors  (EIC)  - set to the EXTINT numbers MCC/PPS assigned
 * ---------------------------------------------------------------------- */
#define HAL_EIC_LAP_CAR1               EIC_PIN_0
#define HAL_EIC_LAP_CAR2               EIC_PIN_1
#define HAL_EIC_Initialize()           EIC_Initialize()
#define HAL_EIC_CallbackRegister(p, cb, ctx) EIC_CallbackRegister((p), (cb), (ctx))
#define HAL_EIC_Enable(p)              EIC_InterruptEnable(p)
#define HAL_EIC_Disable(p)             EIC_InterruptDisable(p)

/* -------------------------------------------------------------------------
 * WS2812 SPI  (SERCOM1 SPI host, DO = PAD0 = PA7 on XPRO header J900-11)
 * ---------------------------------------------------------------------- */
#define HAL_LEDSPI_Initialize()        SERCOM1_SPI_Initialize()
#define HAL_LEDSPI_Write(buf, n)       ((void)SERCOM1_SPI_Write((buf), (n)))
#define HAL_LEDSPI_IsBusy()            SERCOM1_SPI_IsBusy()

/* -------------------------------------------------------------------------
 * ATA8510 433 MHz transceiver  (Click in mikroBUS socket 2, J905)
 *   SPI    : SERCOM4 host, blocking PLIB -> WriteRead returns when done
 *   CS     : PB9  active low   (also SWDIO / SW801 - see MCC_Setup_BZ6.md)
 *   NRESET : PC10 active low   (also green RGB LED)
 *   NPWRON : PC11 active low   (mikroBUS 2 PWM pin, also RMII_EN)
 *   IRQ    : PE3  active low   (also SW800 / BTN2), input with pull-up
 * ---------------------------------------------------------------------- */
#define HAL_ATA_SPI_WriteRead(tx, txn, rx, rxn) \
    SERCOM4_SPI_WriteRead((tx), (txn), (rx), (rxn))

#define HAL_ATA_GPIO_Init()            do { ATA_CS_OutputEnable();     ATA_CS_Set();     \
                                            ATA_NRESET_OutputEnable(); ATA_NRESET_Set(); \
                                            ATA_NPWRON_OutputEnable(); ATA_NPWRON_Set(); \
                                            ATA_IRQ_InputEnable(); } while (0)
#define HAL_ATA_CS_Low()               ATA_CS_Clear()
#define HAL_ATA_CS_High()              ATA_CS_Set()
#define HAL_ATA_NRESET_Low()           ATA_NRESET_Clear()
#define HAL_ATA_NRESET_High()          ATA_NRESET_Set()
#define HAL_ATA_NPWRON_Low()           ATA_NPWRON_Clear()
#define HAL_ATA_NPWRON_High()          ATA_NPWRON_Set()
#define HAL_ATA_IRQ_Get()              ((uint8_t)ATA_IRQ_Get())     /* 0 = asserted */

/* -------------------------------------------------------------------------
 * Status LED (User LED D801, PB7, active LOW)
 * The SW800 demo button is gone (PE3 is now ATA_IRQ); use the BLE DEMO command.
 * ---------------------------------------------------------------------- */
#define HAL_LED0_Init()                do { LED0_OutputEnable(); LED0_Set(); } while (0)
#define HAL_LED0_On()                  LED0_Clear()
#define HAL_LED0_Off()                 LED0_Set()

#endif /* BOARD_HAL_H */
