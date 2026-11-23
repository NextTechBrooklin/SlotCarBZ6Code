# MCC Setup: SlotCar on the PIC32 BZ6 Curiosity Board (EV31U42A)

This guide configures MCC Harmony for the SlotCar port. The board is the EV31U42A, which carries a PIC32WM-BZ6204UE module.

When the guide is done you have:
- a generated Harmony project with the BLE Transparent UART **server** (Peripheral role), advertising as `SLOTCAR_MASTERS`;
- the peripherals that the ported code in `config.mcc/src/app/` expects.

**Rule:** every peripheral name the application uses is listed in `config.mcc/src/app/board_hal.h`. If you pick a different TC, SERCOM or pin name in MCC, change that file only.

References are in this folder:
- `PIC32-BZ6-Curiosity-Board-User-Guide-DS00006006 (1).pdf` (pin tables 3-7 / 3-8 / 3-13 / 3-15 / 3-16)
- `PIC32CX-BZ6-and-PIC32WM-BZ6-Family-Data-Sheet-00005998.pdf` (PPS tables 7-x)

A known-good reference project is `wireless_apps_pic32_bz6/apps/ble/building_blocks/peripheral/profiles_services/peripheral_trp_uart` on GitHub (Microchip-MPLAB-Harmony). Open it side by side if any MCC menu name below differs in your version.

---

## Step 0: Tools
1. Install MPLAB X IDE v6.25 or later (or MPLAB for VS Code). XC32 v5.10 is already installed on this PC, which is fine.
2. Install the MCC plugin, v5.6.2 or later.
3. In MCC, open **Content Manager** and install the content listed in `package.yml` of `wireless_apps_pic32_bz6`:
   - `wireless_pic32cxbz_wbz`, `wireless_ble`
   - `csp`, `core`, `dev_packs`, `CMSIS_5`
   - `crypto`, `wolfssl`, `littlefs`, `zlib`
4. Install the **PIC32CX-BZ6_DFP** device pack: Tools → Packs.

## Step 1: Create the project
1. File → New Project → **Microchip Embedded → 32-bit MCC Harmony Project**.
2. Set the location and names:
   - Framework path: your Harmony content folder.
   - Project location: `C:\Aloe\Wireless\PodCast_Files\SlotCar\SlotCarBZ6`
   - Folder: `firmware`
   - Name: `SlotCarBZ6`
3. Device: **PIC32WM_BZ6204UE**. Finish, and MCC opens.

## Step 2: BLE stack and dependencies
1. In **Device Resources**, go to Wireless → Drivers → BLE and add **BLE Stack**.
2. Click **Yes** on every "activate / connect dependency" prompt. The prompts typically add FreeRTOS, Device Support (RF System, PMU, Clock), PDS, NVM, Crypto and BLE Library.
3. Do not delete any auto-added component.

## Step 3: BLE Stack → GAP (component *BLE Stack*, Configuration Options)

| Setting | Value |
|---|---|
| Device Name | `SLOTCAR_MASTERS` (same as the RNBD451 `SN,` command) |
| GAP Peripheral | ✔ |
| GAP Central | ☐ |
| Advertising → Enable | ✔ |
| Advertising type | Connectable undirected (ADV_IND) |
| Advertising interval min / max | 160 / 160 (×0.625 ms = 100 ms) |
| Advertising data → Local Name | ✔ (Complete Local Name) |
| Advertising data → Service UUID | ✔ if your app filters by service. Use the Transparent Service 128-bit UUID `49535343-FE7D-4AE5-8FA9-9FAFD205E455`; otherwise put it in scan response |
| Security → Pairing | ☐ off (the RNBD451 build was open) |
| GATT → ATT MTU | 247 |
| PHY → 2M | ✔ (optional) |

## Step 4: Transparent Profile (the RNBD451 "Transparent UART")
1. Go to Device Resources → Wireless → BLE → **Profiles** and add **Transparent Profile**.
2. Set the role to **Server** ✔ and Client ☐. MCC adds **Transparent Service** automatically.
3. Leave the service and characteristic UUIDs at their defaults. They match the RNBD451, so the existing phone or Python app connects without changes.

## Step 5: FreeRTOS (component *FreeRTOS*)

| Setting | Value |
|---|---|
| Tick rate (Hz) | **1000** (`os_port.h` assumes 1 ms ticks) |
| Total heap size | keep the BLE default, then **add about 8 KB** for the RACE task (4 KB stack) and the queues |
| Memory management | Heap_4 (default) |
| Use timers / mutexes | defaults |

## Step 6: Clocks (*Clock Configuration* tab)
- Keep the Device Support defaults: system clock 64 MHz, and the 32.768 kHz **SOSC crystal** for BLE low-power timing.
- Write down the GCLK frequency feeding **TC8/TC9**, **TC2/TC3**, **SERCOM1** and **SERCOM4**. You need it in steps 8-10b. The example values below assume 64 MHz.

## Step 7: Debug console, SERCOM0 USART via the on-board MCP2200
1. Add **SERCOM0** and set Operating mode = USART with internal clock.
2. Baud 115200, 8N1, interrupt mode ✔.
3. Pads: **TX = PAD0 (PA5)**, **RX = PAD1 (PA6)** (user guide table 3-6).
4. Add **STDIO** (Tools / System Services) and connect its UART dependency to SERCOM0. `printf` now appears on the board's MCP2200 virtual COM port (USB-C J100).
5. If the BLE stack already added a UART for BLE logging, reuse that one instead.

## Step 8: Motor PWM, TC8 (two cars on one timer)
The H-Bridge 4 Click PWM inputs are IN2 (mikroBUS1 RST, **PA15**) and IN3 (mikroBUS1 INT, **PE4**). TC0 cannot reach PA15, because PA15 is in PPS output group 5 only. **TC8** is the one TC whose two outputs reach both pins: **TC8_WO1 → PA15** (group 5) and **TC8_WO0 → PE4** (group 1).

1. Add **TC8**.
2. Settings:

   | Setting | Value |
   |---|---|
   | Operating mode | **Compare** |
   | Counter mode | **8-bit** |
   | Waveform | **Normal PWM (NPWM)** |
   | Prescaler | GCLK/256 → 250 kHz tick at 64 MHz |
   | Period | **255** → about 977 Hz (SAM build: 915 Hz) |
   | Compare 0 / Compare 1 | 0 / 0 |
   | Waveform output | WO0 ✔, WO1 ✔ |
   | Interrupts | none |

3. The application uses `TC8_CompareStart`, `TC8_Compare8bitMatch1Set` (car 1, WO1 → IN2) and `TC8_Compare8bitMatch0Set` (car 2, WO0 → IN3). Note that car 1 is on **Match1**.
4. Fallback: if your MCC version doesn't offer TC8_WO0/WO1 on these pins, use **TCC0** in single-slope PWM with WO1 → PA15 and WO0 → PE4. Then change the `HAL_PWM_*` macros in `board_hal.h` to the TCC0 PLIB calls.

## Step 9: Lap timer, TC2
1. Add **TC2**.
2. Settings:

   | Setting | Value |
   |---|---|
   | Operating mode | **Timer** |
   | Counter mode | **16-bit** |
   | Prescaler | GCLK/256 (250 kHz) |
   | Timer period | **50 ms** (period about 12500) |
   | Period / overflow interrupt | ✔ |

3. The lap-time maths reads `TC2_TimerFrequencyGet()`, so any prescaler works. Keep the period under 65535 and roughly 50 ms.

## Step 10: WS2812 4x4 matrix, SERCOM1 SPI host (XPRO header J900)
mikroBUS 2 now holds the ATA8510 Click, which uses SERCOM4 (step 10b). The WS2812 data line moves to the **XPRO header J900** on **SERCOM1**.

1. Add **SERCOM1** and set Operating mode = SPI **Host** (Master).
2. Settings:

   | Setting | Value |
   |---|---|
   | SPI mode | 0 (CPOL 0, CPHA 0) |
   | Data order | MSB first |
   | Character size | 8 bit |
   | Baud | **4,000,000** (64 MHz → BAUD = 7 → exactly 4.0 MHz) |
   | Data out pad | **DO = PAD0 (PA7, J900-11)**, SCK = PAD1 (PA8, J900-12). The LEDs don't use SCK |
   | Receiver | disabled |
   | Hardware SS (MSSEN) | disabled |
   | Interrupt mode | ✔ (code calls `SERCOM1_SPI_Write` + `SERCOM1_SPI_IsBusy`) |

3. **Check the timing:** 4.0 MHz gives 250 ns per SPI bit. A 0-bit is then T0H = 500 ns and a 1-bit is T1H = 750 ns, both within the WS2812 spec.
4. If your SERCOM clock is not 64 MHz, choose a BAUD that lands between 3.6 and 4.4 MHz.
5. PA7 and PA8 are also the mikroBUS SDA/SCL pins on both sockets. The ATA8510 and H-Bridge Clicks don't use I²C, so there's no conflict. Don't add an I²C Click while the LEDs are on SERCOM1.
6. **Wiring** (WS2812 4x4 matrix → J900): DIN → **J900-11 (PA7)**, VCC → J900-20 (3.3 V) or an external 5 V, GND → J900-19. If you power the matrix from 5 V, check that the WS2812 accepts a 3.3 V data level.

## Step 10b: ATA8510 433 MHz transceiver, SERCOM4 SPI host (mikroBUS 2, J905)
The ATA8510 Click goes in **mikroBUS 2 (J905)**. It polls the two SAMD21 joystick remotes (A → car 1, B → car 2). The driver is `config.mcc/src/app/ata8510.c` plus `ata8510/SYS_ATA851X.c`, ported unchanged from SlotCarSam.

1. Add **SERCOM4** and set Operating mode = SPI **Host** (Master).
2. Settings:

   | Setting | Value |
   |---|---|
   | SPI mode | 0 (CPOL 0, CPHA 0) |
   | Data order | MSB first |
   | Character size | 8 bit |
   | Baud | **500,000** (same as the SAM build) |
   | Data out pad | **DO = PAD3 (PA9)**, **SCK = PAD1 (PE5)** |
   | Data in pad | **DI = PAD0 (PA4)** |
   | Receiver | **enabled** |
   | Hardware SS (MSSEN) | **disabled**. CS is the GPIO `ATA_CS` on PB9, which the driver toggles around each telegram. PAD2 is PA10, the H-Bridge EN pin |
   | Interrupt mode | **☐ off (blocking)**. `SYS_ATA851X.c` expects `SERCOM4_SPI_WriteRead()` to return only after the transfer has finished |

3. GPIO for the Click is configured in step 12: `ATA_CS` PB9, `ATA_NRESET` PC10, `ATA_NPWRON` PB5, `ATA_IRQ` PE3.

## Step 11: Lap sensors, EIC
1. Add **EIC**.
2. Enable two channels and set the same options on each:

   | Channel | Lap sensor |
   |---|---|
   | **EXTINT0** | car 1 |
   | **EXTINT1** | car 2 |

   Options for both channels: sense = **Rising edge**, Interrupt ✔, Filter ✔, Debounce ✔ (if offered).
3. If MCC or PPS forces other EXTINT numbers on your chosen pins, update `HAL_EIC_LAP_CAR1/2` in `board_hal.h`.

## Step 12: Pin Configuration (Pin Table and PPS)
The H-Bridge 4 Click goes in **mikroBUS 1 (J903)**. The **ATA8510 Click** goes in **mikroBUS 2 (J905)**. The WS2812 matrix is wired to the **XPRO header J900** (step 10).

H-Bridge 4 Click → mikroBUS 1 → BZ6 pin. The Click column comes from the MikroE `HBRIDGE4_MAP_MIKROBUS` driver; the pin column comes from user guide table 3-7.

| Click | mikroBUS 1 | BZ6 pin | Use |
|---|---|---|---|
| IN1 | AN (1) | PB10 | car 1, held low |
| IN2 | RST (2) | PA15 | **car 1 PWM** |
| EN | CS (3) | PA10 | driver enable |
| IN3 | INT (15) | PE4 | **car 2 PWM** |
| IN4 | PWM (16) | PD1 | car 2, held low |

Car 1 = IN1/IN2 and car 2 = IN3/IN4. PWM goes on IN2 and IN3, with IN1 and IN4 held low, which is the same drive scheme as the SAM build.

| Pin | Function in MCC | Custom name | Notes |
|---|---|---|---|
| PA5 | SERCOM0_PAD0 | | debug TX → MCP2200 |
| PA6 | SERCOM0_PAD1 | | debug RX ← MCP2200 |
| PA9 | SERCOM4_PAD3 | | ATA8510 MOSI (mikroBUS2 MOSI) |
| PE5 | SERCOM4_PAD1 | | ATA8510 SCK (mikroBUS2 SCK) |
| PA4 | SERCOM4_PAD0 | | ATA8510 MISO (mikroBUS2 MISO) |
| PB9 | GPIO Out, **high** | `ATA_CS` | ATA8510 CS (mikroBUS2 CS), active low. **Also SWDIO and SW801**, see the note below |
| PC10 | GPIO Out, **high** | `ATA_NRESET` | ATA8510 NRESET (mikroBUS2 RST), active low. Also the green RGB LED |
| PB5 | GPIO Out, **high** | `ATA_NPWRON` | ATA8510 NPWRON (mikroBUS2 AN), active low. Analog off. Also BAT_MON |
| PE3 | GPIO In, pull-up ✔ | `ATA_IRQ` | ATA8510 IRQ (mikroBUS2 INT), active low. Also SW800 / BTN2 |
| PA7 (J900-11) | SERCOM1_PAD0 | | WS2812 DIN |
| PA8 (J900-12) | SERCOM1_PAD1 (optional) | | SPI SCK (the WS2812 doesn't need it) |
| PA15 | **TC8_WO1** (PPS group 5) | | Car 1 PWM → Click IN2 (mikroBUS1 RST) |
| PE4 | **TC8_WO0** (PPS group 1) | | Car 2 PWM → Click IN3 (mikroBUS1 INT) |
| PB10 | GPIO Out, low | `HB_IN1` | Click IN1 (mikroBUS1 AN). Leave its analog/ADC function off |
| PD1 | GPIO Out, low | `HB_IN4` | Click IN4 (mikroBUS1 PWM pin, used here as GPIO) |
| PA10 | GPIO Out, low | `PWM_EN` | Click EN (mikroBUS1 CS). Must not be SERCOM4_PAD2 (MSSEN off) |
| PB11 (J701-13) | **EXTINT0** (PPS), pull-up ✔ | | lap sensor car 1 |
| PB12 (J701-11) | **EXTINT1** (PPS), pull-up ✔ | | lap sensor car 2 |
| PB7 | GPIO Out, **high** | `LED0` | User LED D801, active low |

Notes:
- **PPS:** in the Pin Table, a pin's drop-down only lists the functions its PPS group allows (data sheet tables 7-8 to 7-14). The Click pins are fixed by the socket, so PA15 and PE4 must use TC8 (or the TCC0 fallback in step 8). If an EXTINTx is missing on a lap-sensor pin, choose another free J701 pin and update this table.
- The custom names must be **exactly** `HB_IN1`, `HB_IN4`, `PWM_EN`, `LED0`, `ATA_CS`, `ATA_NRESET`, `ATA_NPWRON` and `ATA_IRQ`. MCC then generates `HB_IN1_Set()` and the matching functions that `board_hal.h` uses. Delete the old `SWITCH` name from PE3.
- **PB9 = SWDIO.** Once the firmware drives PB9 as `ATA_CS`, SWD debugging can drop out after the app starts. Programming normally still works, because the PKOB4 holds MCLR in reset while it connects. If the board stops responding to the debugger, use "Hold in reset" or erase the device (user guide Appendix B, PKOB4 recovery). Button SW801 is on the same pin: don't press it while the remotes are polled.
- **PE3 = SW800.** Pressing SW800 pulls the ATA8510 IRQ line low. That only causes a spurious event read, which the driver ignores, so the demo is now started with the BLE `DEMO` command.
- **PC10 = green LED.** The green RGB LED follows NRESET.

## Step 13: Generate and run a smoke test
1. Click **Generate**, then build. The empty project must compile.
2. Program the board through the PKOB4 (USB-C J100).
3. Open the **Microchip Bluetooth Data (MBD)** app → BLE UART → scan. **SLOTCAR_MASTERS** must appear and connect.

## Step 14: Add the application code
**Done.** The application lives in `config.mcc/src/app/`. The VS Code project's `**/*` file set picks it up automatically, and it's included as `"app/xxx.h"` from `config.mcc/src`. These edits are already applied in the generated files. When MCC asks to merge on regenerate, **keep** them:

   | File | Edit (marked `SlotCar`) |
   |---|---|
   | `app.h` | `APP_MSG_BLE_LINK_TX` message ID |
   | `app.c` | includes; `BLE_Link_RequestFlush()`; `BLE_Link_Initialize()`, `RaceTask_Create()` and `BLE_GAP_SetAdvEnable()` in `APP_STATE_INIT`; `APP_MSG_BLE_LINK_TX` → `BLE_Link_FlushTx()` |
   | `app_ble/app_ble_handler.c` | CONNECTED → `BLE_Link_OnConnected`; DISCONNECTED → `BLE_Link_OnDisconnected` + re-advertise; TX_BUF_AVAILABLE → `BLE_Link_FlushTx` |
   | `config/default/freertos_hooks.c` | `#include "definitions.h"` and `app_idle_task()` in the idle hook (from `app_user_edits.c`, whose `#error` is now commented out) |

**Transparent Profile (Server):** added in MCC. `app_ble/app_trsps_handler.c` forwards every event to `BLE_Link_TrspsEvtHandler()` (marked `SlotCar`, keep it on regenerate). The stream opens on `BLE_TRSPS_EVT_TX_STATUS` = `TX_OPENED`, which is when the phone enables TX notifications. `BLE_TRSPS_SendData()` refuses to send before that. Phone commands arrive on `BLE_TRSPS_EVT_RECEIVE_DATA`.

**Device pack:** DFP **1.5.90** has broken configuration-bit data: every `#pragma config` line in `initialization.c` fails with "Malformed configuration definition file". Select **PIC32CX-BZ6_DFP 1.4.74** in the project settings. With 1.4.74 the whole project builds and links on XC32 v6.00.

4. Build and program.

## Step 15: Bench test (MBD app → BLE UART, text mode)

At boot the console should show `ATA8510: power on`, a `ROM version` line (not 0x00 or 0xFF), `IRQ pin = 1` and `poll master active`. If the ROM version reads 0x00 or 0xFF, check the SPI pads and the CS/NRESET/NPWRON wiring.


| Send | Expected |
|---|---|
| *(connect)* | User LED D801 on. Console shows `BLE: connected`, then `stream open` |
| `PRACTICE:1` | yellow chase on the matrix, motors enabled, console shows `ATA8510: A M:<mag> C:<n>` for each joystick reply |
| *(move remote A / B joystick)* | car 1 / car 2 follows the joystick (× 0.75 duty cap). Switching a remote off coasts its car on the next poll cycle |
| `PRACTICE:0` | motors off, LEDs clear |
| `START_RACE` | red rows countdown, then green, then a 3-lap race driven by the remotes. `<C1,x.xxx>` / `<C2,..>` frames arrive. A BLE disconnect no longer aborts the race |
| `STOP_RACE` | aborts the race |
| `GHOST:1` / `GHOST:0` | autonomous 70-85 % ramp with `<R1,..>` frames / stop |
| `DEMO` | demo mode (replaces the SW800 button). Stops on `STOP_RACE` or disconnect |
