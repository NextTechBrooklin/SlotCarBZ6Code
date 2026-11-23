# SlotCarSam (SAM E51) → SlotCarBZ6 (PIC32WM-BZ6204UE): Porting Notes

## Source file mapping

| SAM E51 file | BZ6 file | Change |
|---|---|---|
| `main.c` superloop | `race_task.c` | Becomes a FreeRTOS task (priority idle+1, 4 KB stack, 10 ms loop). Created from `app.c` |
| `rnbd451.c/.h` | `ble_link.c/.h` | On-chip TRPS. No `$$$` config: name, advertising and service are set in MCC. `%CONNECT%` parsing is replaced by stack events. TX is queued and sent from the APP task |
| `ata8510.c/.h`, `ata8510/SYS_ATA851X.*` | `app/ata8510.c/.h`, `app/ata8510/SYS_ATA851X.*` | **Restored.** Same RF protocol, so the SAMD21 joystick remotes are unchanged. SPI is SERCOM4 (blocking PLIB) on mikroBUS 2. GPIO goes through `board_hal.h`. ms delays are `vTaskDelay` (one poll cycle is ≤ ~45 ms, and BLE keeps running). µs delays use the DWT cycle counter. The phone-throttle module `remote.c/.h` (`SPD1:`/`SPD2:`) has been removed |
| `motorPWM.c/.h` | same | TC8 WO1 + WO0 (was TC0 + TC1). 75 % duty cap kept |
| `LEDs.c/.h` | same | SERCOM1 SPI at 4.0 MHz on the XPRO header (was SERCOM2 at 4.29 MHz). Delays use `vTaskDelay` |
| `race.c` | same | TC2 lap timer (was TC4). Loop yields 5 ms and aborts on `STOP_RACE` or BLE loss |
| `ghost.c` | same | Ramp timed by the RTOS tick (bug fix, see below). Stops on BLE loss |
| `raceDemo.c` | same | No longer an infinite loop: stops on `STOP_RACE` or stream closed |
| `practice.c` | unchanged | The remotes drive the cars while practice is active |
| (new) `board_hal.h` | | Single place for every MCC PLIB and pin name |
| (new) `os_port.h` | | `OS_DelayMs` and `OS_Millis` (replace `SYSTICK_*`) |
| (removed) `app_integration_snippets.c` | | Already applied to the MCC-generated `app.h`, `app.c`, `app_ble_handler.c` and `freertos_hooks.c` (marked `SlotCar`). The TRSPS handler is `BLE_Link_TrspsEvtHandler()` in `ble_link.c`. App sources are in `config.mcc/src/app/` |

## BLE protocol (Transparent UART, ASCII)

**Contract:** `C:\Aloe\Wireless\PodCast_Files\SlotCarPythonApp\protocol.md` (Slot Car Duel PC app, protocol v4.1). The firmware follows it. The device advertises as `SlotCarMasters`, which passes the app's `Slot*` filter, and uses the standard Microchip TRS service, TX and RX UUIDs.

**App → car.** One command per GATT write; CR, LF or `;` also end a command. Either `:` or `,` works as the separator.

| Command | Sent by PC app | Meaning |
|---|---|---|
| `START_RACE` | ✔ at lights-out, no terminator | 3-lap race (`RACE_NUM_LAPS`). It ends practice if practice is on, goes green **immediately** (the app has already done the countdown) and replies `START` |
| `GHOST:n` | ✔ `GHOST:5` / `GHOST:0` | n > 0 starts ghost mode; 0 stops it. The ghost loop only listens for `GHOST:0` |
| `PRACTICE:n` | ✔ `PRACTICE:45` / `PRACTICE:0` | n > 0 starts practice (the remotes drive the cars); 0 stops it |
| `STOP_RACE` | ✘ firmware extra, e.g. from the MBD app | aborts a race or the demo |
| `DEMO` | ✘ firmware extra, e.g. from the MBD app | runs the demo sequence. It replaces the SW800 button, whose pin PE3 is now the ATA8510 IRQ |

The throttle does **not** go over BLE. The two SAMD21 joystick remotes are polled over 433 MHz by the ATA8510 (remote A → car 1, remote B → car 2).

**Car → app.** Every line ends in `\r\n`; the SAM build used `\n\r`, and the app accepts both.

| Frame | When |
|---|---|
| `START` | once, at the start of `Race_Run` (green) |
| `<C1,s.sss` / `<C2,s.sss` | lap time in seconds. **Races only.** At most 3 per lane: once a car finishes, further crossings are ignored, and the car is coasted and stops listening to its remote |
| `<R1,n` / `<R2,n` | 0-100. In **race and practice** it is the joystick magnitude, sent only when it changes and at most every 100 ms per lane, ending with `0` when the car stops or the mode ends. In ghost and demo modes it is the motor %, as before |

**Laps outside a race** (the idle monitor) are printed on the console only. The app would treat a `<Cn>` that arrives before the start as the start of a race.

**Leaving a race.** A race ends when both cars have done 3 laps, on `STOP_RACE`, or when **any other command arrives** (`GHOST:n`, `PRACTICE:n`, `START_RACE`, `DEMO`). The PC app's Reset and Restart send nothing themselves, so the firmware treats the app's next command as "the app has left this race": it ends the race and then carries out that command. `START_RACE` during a race starts a new one.

## Pin map

| Function | SAM E51 | BZ6 Curiosity |
|---|---|---|
| Car 1 PWM (Click IN2) | PA04 TC0/WO0 | PA15 TC8/WO1 (mikroBUS1 RST) |
| Car 2 PWM (Click IN3) | PA07 TC1/WO1 | PE4 TC8/WO0 (mikroBUS1 INT) |
| HB IN1 / IN4 / EN | PA02 / PA10 / PA18 | PB10 (AN) / PD1 (PWM) / PA10 (CS) on mikroBUS1 |
| Lap sensor 1 / 2 | PB09 EIC9 / PB08 EIC8 | PB11 EXTINT0 / PB12 EXTINT1 (J701) |
| Lap timer | TC4 | TC2 |
| WS2812 | PA12 SERCOM2 | PA7 SERCOM1 PAD0 (XPRO J900-11) |
| LED0 (active low) | PA14 | PB7 User LED D801 |
| Button | PA15 | removed. Use the BLE `DEMO` command |
| Console | SERCOM5 PB16/17 | SERCOM0 PA5/PA6 → MCP2200 VCOM |
| BLE | RNBD451 on SERCOM4 + RST/RTS/CTS | on-chip, no pins |
| 433 MHz RF SPI | ATA8510 on SERCOM1 (PA16/17/19) | SERCOM4 on mikroBUS 2: SCK PE5 (PAD1), MISO PA4 (PAD0), MOSI PA9 (PAD3) |
| ATA8510 CS / NRESET / NPWRON / IRQ | PB5 / PA6 / PA11 / PB14 | PB9 (`ATA_CS`, also SWDIO) / PC10 (`ATA_NRESET`, also green LED) / PB5 (`ATA_NPWRON`) / PE3 (`ATA_IRQ`, also SW800) |

## Behaviour differences and fixes

1. **Ghost-mode ramp bug fixed.** On the SAM build, `Ghost_Run()` stopped TC4 and then timed its 100 ms ramp steps from TC4. The ramp therefore never left 70 %. The BZ6 version uses `OS_Millis()`, so the 70-85 % random ramp now really runs.
2. **Disconnect behaviour.** The RF remotes drive the cars, so a BLE disconnect no longer stops a race or practice, as on the SAM build. A remote that stops answering coasts only its own car. Ghost and demo modes still stop on a disconnect.
3. `RaceDemo_Run()` can now exit; on the SAM build it never returned.
4. The PWM frequency is about 977 Hz instead of 915 Hz, which is negligible for the TB67H450.
5. `Race_UpdatePwm()` (the random ramp in `race.c`) was unused on the SAM build and is still unused. It is kept and marked `unused`.
6. The demo starts with the BLE `DEMO` command instead of a button press.
7. Command matching is now exact per command line (`strcmp`), where the SAM build searched for a substring in a 32-byte buffer. Send one command per write.

## Verification status
- All ported `.c` files pass `pic32c-gcc` (XC32 v5.10) `-Wall -Wextra -fsyntax-only` with zero warnings, against stub PLIB, FreeRTOS and TRPS headers.
- **Not yet verified:** a full link against the real MCC-generated code. The Harmony BZ6 content is not installed on this PC yet. After MCC Step 13, check these against the generated headers:
  - `BLE_TRSPS_*` event field names,
  - `TC_TIMER_STATUS_OVERFLOW` versus `TC_TIMER_STATUS_PERIOD_MATCH` (BZ6 PLIB naming),
  - the `EIC_PIN_x` enum names.
- Hardware tests: see `MCC_Setup_BZ6.md` Step 15.
