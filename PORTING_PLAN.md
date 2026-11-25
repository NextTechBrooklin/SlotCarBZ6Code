# Plan: Port SlotCarSam (SAM E51) to the PIC32 BZ6 Curiosity Board (EV31U42A) using on-chip BLE

## Context
The track/car controller `SlotCarSam` (`C:\Aloe\Wireless\ATEProjects2026\ATESlotFinal_M26PHX\SC51\SlotCarMasters-test-merge\SlotCarSam`) runs today on an **ATSAME51J20A**. It uses:
- an external **RNBD451** BLE module (Transparent UART) to talk to the phone/PC app, and
- an **ATA8510** 433 MHz radio to poll two SAMD21 joystick remotes.

**The goal:** move the car side to the **PIC32WM-BZ6204UE** module on EV31U42A.
- The BZ6's own BLE stack replaces the RNBD451.
- The ATA8510 link goes away. The **phone app becomes the only controller**, as you chose: car side only, with a phone as the remote.
- The joystick remotes and the ATA8510 firmware are out of scope.

All new files go in `C:\Aloe\Wireless\PodCast_Files\SlotCar`. The board user guide (DS00006006) and the data sheet (DS00005998) are already there.

## Target architecture
- **Software:** MPLAB Harmony 3 for BZ6 (`wireless_pic32cxbz_wbz` / `wireless_ble` plus CSP) with **FreeRTOS**, which the BLE stack requires.
- **BLE role:** a **Peripheral** running the **Transparent Profile Server (TRPS / TRS)**.
  - It uses the same Microchip Transparent UART service UUID that the RNBD451 exposes (`49535343-FE7D-4AE5-8FA9-9FAFD205E455`). The existing phone/Python app, or the MBD app, should therefore connect without changes.
  - Advertised name: `SLOTCAR_MASTERS`.
- **Tasks:**
  - **APP task** (MCC-generated `app.c`): owns BLE events and connection state, and receives TRS data. It forwards each complete text command to the race task through a FreeRTOS queue.
  - **RACE task** (new): runs the existing race, ghost, practice and demo logic. The current blocking `delay_ms` calls become `vTaskDelay`, so the module logic barely changes.
  - Outbound frames (`<C1,..>`, `<R1,..>`) go through one `BLE_Send(const char*)` wrapper that calls `BLE_TRSPS_SendData()`. It replaces the RNBD451 SERCOM4 writes.

## Code changes (module by module)
Source modules are under `...\SlotCarSam\config.mcc\src\`.

| Existing file | Action |
|---|---|
| `main.c` | Replace with the Harmony FreeRTOS `main.c`/`tasks.c`. Move the superloop logic into `race_task.c`. Command parsing (`START_RACE`, `GHOST:n`, `PRACTICE:n`, `,` or `:` separator) is reused from `rnbd451.c:114-141`. |
| `rnbd451.c/.h` | **Delete.** Replace with `ble_link.c/.h`, which provides `BLE_Send()`, `BLE_IsConnected()` and `BLE_IsStreamOpen()` (true once the TRS CCCD is enabled). It also provides a line assembler for received TRS bytes. `RNBD451_IsStreamOpen()` calls become `BLE_IsStreamOpen()`. |
| `ata8510.c/.h`, `ata8510/SYS_ATA851X.*` | **Delete.** Joystick control moves to new phone commands: `SPD1:n` and `SPD2:n` (0-100), which call `MotorPWM_SetSpeed()`. A watchdog coasts a car if no `SPDx` arrives for 500 ms, matching today's "no response → coast". `Race_Run` and practice mode replace each `ATA8510_PollCycle()` call with "apply latest phone speeds". |
| `race.c/.h` | Keep the logic. Change only the timer and EIC callback registration names to the BZ6 PLIB names, and the TC tick rate constant used by `Race_GetTimestampTicks`. |
| `ghost.c`, `practice.c`, `raceDemo.c` | Keep. Replace delays with `vTaskDelay`. `RaceDemo_Run`'s infinite loop must exit on BLE disconnect or a command. |
| `motorPWM.c/.h` | Keep the API. Re-point it to the BZ6 TC/TCC PWM PLIB (`TCx_CompareXSet`) and the new GPIOs. Keep the 75 % duty cap (`PWM_DUTY_SCALE_PCT`). |
| `LEDs.c/.h` (WS2812 via SPI MOSI, 5-bit encoding) | Keep the encoding. Recompute the SPI baud for the BZ6 SERCOM clock so it stays near 4.29 MHz, and re-point it to the new SERCOM SPI PLIB. |
| debug `printf` | Route to SERCOM0, which reaches the PC through the on-board MCP2200 USB-UART (virtual COM port). |

## Pin and peripheral mapping (SAM E51 → BZ6 Curiosity)
Pins below come from the user guide tables 3-7, 3-8, 3-13, 3-15 and 3-16. Put the **H-Bridge 4 Click in mikroBUS socket 1 (J903)**, keeping each signal on the same mikroBUS position it uses on the current SAM base board. Before wiring, check the Click's pinout against the mikroBUS 1 column.

| Function | SAM E51 | BZ6 suggestion | Peripheral |
|---|---|---|---|
| Car 1 PWM (Click IN2) | PA04, TC0 WO0 | PA15 (mikroBUS1 RST) | TC8 WO1 via PPS |
| Car 2 PWM (Click IN3) | PA07, TC1 WO1 | PE4 (mikroBUS1 INT) | TC8 WO0 via PPS |
| H-bridge IN1 / IN4 | PA02 / PA10 | PB10 (mikroBUS1 AN) / PD1 (mikroBUS1 PWM) | GPIO out, low |
| H-bridge enable `PWM_EN` | PA18 | PA10 (mikroBUS1 CS) | GPIO out |
| Lap sensor car 1 / car 2 | PB09 EIC9 / PB08 EIC8 | J701 free pins, e.g. PB11 / PB12 | EIC EXTINTx via PPS, rising edge, pull-up |
| Lap timer | TC4 16-bit, ~50 ms overflow | a free TC (e.g. TC2) 16-bit, ~50 ms period | TC timer with interrupt |
| WS2812 4x4 matrix | PA12, SERCOM2 MOSI | PA9 (SERCOM4 PAD3, mikroBUS2 MOSI) | SERCOM4 SPI host, TX only |
| Status LED0 (active low) | PA14 | **PB7 User LED D801** (active low, same polarity) | GPIO out |
| Button `SWTICH` | PA15 | **PE3 SW800 / BTN2** | GPIO input with pull-up, or EIC |
| Debug console | SERCOM5 PB16/PB17 | SERCOM0 PA5 TX / PA6 RX (MCP2200) | USART 115200 |
| BLE | RNBD451 on SERCOM4 | on-chip | BLE stack |

**Pins to avoid:**
- PB9 (SW801) is shared with SWDIO.
- PC7, PC10 and PE0 drive the RGB LED. They are free for status colours if you want them.
- Any pin you choose for PWM or EXTINT must show that function in MCC's PPS pin list (data sheet section 7.12/7.13).

## Step-by-step MCC configuration for the BZ6
The plan file will also be saved as `MCC_Setup_BZ6.md`. Component names follow the BZ6 Application Developer's Guide (DS50003xxx section 4.4/5.2). If a name differs slightly in your MCC version, search by keyword in Device Resources.

1. **Install the tools.**
   - MPLAB X v6.25 or later (or MPLAB for VS Code), XC32 v4.60 or later, and the MCC plugin v5.6.2 or later.
   - In MCC Content Manager, install the BZ6 Harmony content: `wireless_pic32cxbz_wbz`, `wireless_ble`, `csp`, `core`, `dev_packs`, `CMSIS_5`, `crypto`, `wolfssl`, `zlib`, `littlefs`. Use the versions listed in `wireless_apps_pic32_bz6/package.yml`.
2. **Create the project.**
   - File → New Project → Application → 32-bit MCC Harmony Project.
   - Location: `C:\Aloe\Wireless\PodCast_Files\SlotCar\SlotCarBZ6\`, folder `firmware`, name `SlotCarBZ6`.
   - Device: **PIC32WM-BZ6204UE**. Open MCC.
3. **Add the BLE stack.** Device Resources → Wireless → Drivers → BLE → **BLE Stack**.
   - Accept the auto-added dependencies: **FreeRTOS**, **PIC32CX-BZ6 Device Support / RF System**, **PDS**, **Crypto**, **NVM**, and so on.
   - Answer **Yes** to every "auto-activate / connect" prompt.
4. **Configure BLE Stack → GAP.**
   - Device name `SLOTCAR_MASTERS`, Peripheral role **enabled**, Central **disabled**.
   - Advertising enabled with legacy advertising. Interval 100 ms (min 160 / max 160 slots, or similar). Type: connectable undirected.
   - Advertising data: include **Local Name**, and the **Service UUID** of the Transparent Service if your app filters on it.
   - Security: Pairing **disabled**, to match the RNBD451's open behaviour. Enable later if wanted.
   - Optional: raise the ATT MTU to 247 and enable the 2M PHY.
5. **Add the Transparent Profile.** Wireless → BLE → Profiles → **Transparent Profile**.
   - Role: **Server** (Client unchecked). This also pulls in the **Transparent Service**.
   - Keep the default Microchip TRS UUIDs, which match the RNBD451.
6. **Configure FreeRTOS.**
   - Total heap at least the developer's guide minimum (section 5.1, about 40-60 KB). Tick rate 1000 Hz.
   - Later in code, add a RACE task (stack 1024 words, priority below the BLE/APP task).
7. **Check clocks** (Clock Configurator). Keep the device-support defaults: 64 MHz system clock and the 32.768 kHz SOSC crystal for BLE sleep timing. Write down the peripheral GCLK frequency for the TC and SPI calculations.
8. **Add the debug UART.** Peripherals → **SERCOM0** → USART.
   - 115200 8N1, TX on PAD0 (PA5), RX on PAD1 (PA6), interrupt mode.
   - Then add **STDIO** under System Services/Tools and connect it to SERCOM0, so `printf` works over the MCP2200 COM port.
9. **Add the motor PWM.**
   - Add the **TC/TCC** instance(s) whose WO outputs are available on the chosen pins. Mode: PWM (normal PWM), 8-bit or 16-bit.
   - Prescaler and period chosen for about 1 kHz to match today's ~915 Hz. Example: GCLK/64 with PER 255.
   - Initial duty 0. Enable the output on the WO channels that feed Car 1 and Car 2.
10. **Add the lap timer.**
    - Add another **TC** (e.g. TC2) in 16-bit Timer mode.
    - Period interrupt enabled, period about 50 ms. Record the tick rate for `race.c`.
11. **Add the WS2812 SPI.**
    - **SERCOM4** → SPI Host. Mode 0, MSB first, baud about 4.2-4.3 MHz.
    - DO on PAD3 (PA9), SCK on PAD1 (PE5, it may be left unrouted). DI not used. Polled or interrupt mode is fine.
12. **Add the EIC for the lap sensors.** Peripherals → **EIC**.
    - Enable the two EXTINT channels you map via PPS. Rising edge, interrupt enabled, filter and debounce enabled.
13. **Assign pins** (Pin Configuration / Pin Table, PPS).
    - PA5 SERCOM0_PAD0, PA6 SERCOM0_PAD1.
    - PA9 SERCOM4_PAD3.
    - PWM WO pins as chosen.
    - EXTINT pins with **pull-up**.
    - GPIO outputs, with these custom names:

      | Name | Pin | Initial level |
      |---|---|---|
      | `LED0` | PB7 | high = off |
      | `HB_IN1` | (chosen pin) | low |
      | `HB_IN4` | (chosen pin) | low |
      | `PWM_EN` | (chosen pin) | low |

    - GPIO input `SWITCH` on PE3 with pull-up.
    - Make sure PB9 stays SWDIO.
14. **Generate code.** Click Generate, then build the empty project and confirm it compiles. Flash it through the on-board PKOB4 (USB-C J100). Confirm in the MBD app that it advertises as `SLOTCAR_MASTERS`.
15. **Add application code.**
    - In `app.c` `APP_BleStackCb` / `APP_TrspsEvtHandler`, handle:
      - `BLE_GAP_EVT_CONNECTED` / `DISCONNECTED`: set the connection flag and restart advertising on disconnect.
      - `BLE_TRSPS_EVT_CTRL_STATUS`: the stream-open flag.
      - `BLE_TRSPS_EVT_RECEIVE_DATA`: call `BLE_TRSPS_GetData()`, feed the bytes into the line assembler, and post to the race queue.
    - Add the ported modules and `race_task.c`.

## Implementation order
1. Run MCC steps 1-14 and confirm the board advertises.
2. Add `ble_link.c` with an echo test from the MBD app's Transparent UART screen.
3. Port `LEDs.c` and test the colour cycle.
4. Port `motorPWM.c` plus the `SPD1:`/`SPD2:` commands and test with the motors.
5. Port `race.c` (EIC + TC) and test idle-mode `<Cn,secs>` lap frames.
6. Port `Race_Run`, `Ghost_Run`, `Practice_Run` and `RaceDemo_Run`.
7. Write `PORTING_NOTES.md` covering the pin map, the command protocol (including the new `SPDx`) and the known differences.

## Deliverables in `C:\Aloe\Wireless\PodCast_Files\SlotCar`
- The VS Code project `.vscode/BZ6SlotCar.mplab.json` (MCC output in `config.mcc/`), with the ported sources in `config.mcc\src\app\`
- `MCC_Setup_BZ6.md` (the step-by-step MCC guide above, expanded with screenshots-to-take notes)
- `PORTING_PLAN.md` (this plan) and `PORTING_NOTES.md` (pin map plus protocol)

## Verification
- **Build:** the project compiles cleanly with XC32 and the FreeRTOS heap check passes.
- **BLE:**
  - The MBD app or a phone finds `SLOTCAR_MASTERS` and connects to the Transparent UART service.
  - Sending `PRACTICE:1`, then `SPD1:50`, makes car 1 run at 50 % (×0.75 cap).
  - Stopping the `SPD` writes coasts the car within 500 ms.
- **Existing app:** the current phone/Python app connects unchanged. `START_RACE` runs the countdown, and lap frames `<C1,x.xxx>` arrive.
- **Peripherals:**
  - Check the PWM frequency and duty with a scope on the chosen WO pins.
  - Triggering the lap sensors produces the right `<Cn,..>` timing, with intervals under 1 s rejected.
  - The WS2812 matrix shows the colour cycle.
  - `printf` output appears on the MCP2200 COM port.
- **Robustness:** after disconnecting mid-race, the motors coast and advertising restarts.

## Addendum (2026-11-16): ATA8510 remotes restored
This replaces the "phone is the only controller" decision above.
- **Phone throttle removed.** `remote.c/.h` and the `SPD1:`/`SPD2:` commands are gone. BLE still carries `START_RACE`, `STOP_RACE`, `GHOST`, `PRACTICE`, the new `DEMO` command, and the lap and PWM frames.
- **ATA8510 back.** The Click goes in mikroBUS 2 (J905) and polls the two SAMD21 joystick remotes with the unchanged SlotCarSam protocol.
  - SPI: SERCOM4 host, blocking, 500 kHz. SCK PE5, MISO PA4, MOSI PA9.
  - GPIO: CS PB9, NRESET PC10, NPWRON PC11 (mikroBUS 2 PWM pin), IRQ PE3.
- **WS2812 moved** to SERCOM1 PAD0 on PA7 (XPRO J900-11), because PA9 is now ATA8510 MOSI.
- **Demo button removed**, because PE3 (SW800) is now the ATA8510 IRQ. The demo starts with the BLE `DEMO` command.
- **SWD warning.** PB9 doubles as SWDIO. See `MCC_Setup_BZ6.md` step 12.
- See `MCC_Setup_BZ6.md` steps 10, 10b and 12, and `PORTING_NOTES.md`.
