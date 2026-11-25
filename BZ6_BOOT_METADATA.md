# BZ6 boot metadata

The PIC32CX2051BZ62132 boot ROM will not start an application unless a valid **metadata header** sits in front of it in flash. Without one, the ROM falls into Secure Safe mode (a `while (1)` loop), so the board is silent: no UART output, no advertising.

Source: *PIC32CX BZ6 and PIC32WM BZ6 Family Data Sheet*, DS00005998F, **chapter 13, Secure Boot ROM** (pages 241-249). The payload field layout below (offset 0x48 and up) is not in the data sheet text. It comes from the Microchip DFP script `PIC32CX-BZ6_DFP/scripts/properties_device.py` (`fill_bz6_header`) and from Zephyr's `soc/microchip/pic32c/pic32cx_bz/bz6x/gen_signed_hex.py`.

## How the ROM uses it (13.3)

1. After reset the ROM initializes the system, then looks for a valid firmware image. Validity means a valid header (`MCHP` identifier, valid sequence number) and, when required, authentication.
2. It chooses among valid images by sequence number (13.3.2.2), then jumps to the image's destination address (`DST_ADDR`).
3. If no image is valid, it enters **Secure Safe mode** and waits for a power-on reset or a programming tool.
4. On an **unsecured** device (secure boot key invalid or not set) an image with auth method *None* is accepted. On a **secured** device (boot key != 0) the image must be authenticated with ECDSA P-256/SHA-256 or P-384/SHA-384 (Table 13-4). The header this project writes uses auth *None*, so it only boots on unsecured devices such as the development boards.

## Where it lives (Tables 13-2, 13-3)

| Item | Address | Size |
|---|---|---|
| Image 2 metadata (used here) | `0x0100_0000` | 512 bytes |
| Image 2 (the application) | `0x0100_0200` | up to 2,096,640 bytes on the 2 MB variant |

This matches `ROM_ORIGIN 0x1000200` and `ROM_LENGTH 0x1ffe00` in the project linker script. Images 0 and 1 live in boot flash at `0x0080_0000` and `0x0080_8000`.

**Erase granularity.** Flash erases in 4 KB pages (`pagesize="4096"` in the DFP, `NVM_FLASH_PAGESIZE` in the generated NVM code). The header shares its page (`0x0100_0000-0x0100_0FFF`) with the start of the application, so programming the application erases the header. MPLAB writes only what is in the image's loadable program headers and has no "preserve this region" option. The header must therefore be part of the image every time. A header that was programmed once is not kept across reprogramming.

## Header layout written by this project

| Offset | Field | Value | Source |
|---|---|---|---|
| `0x00-0x17` | filler | 0 | data sheet |
| `0x18` | identifier `MCHP` | bytes `50 48 43 4D` (reads as word `0x4D434850`) | data sheet |
| `0x1C-0x3B` | filler | 0 (`0xFFFF_FFFF` until `SEQ_NUM` is programmed, then 0, for ECC) | data sheet, note 1 |
| `0x3C` | `SEQ_NUM` | `0xFFFFFFFE` | data sheet field; value from Zephyr/DFP default |
| `0x40` | `MD_REV` | `0x03` (metadata v3) | data sheet |
| `0x41` | `CONT_IDX` | `1` = firmware image | data sheet |
| `0x42` | `MD_AUTH_MTHD` | `0` = none | data sheet |
| `0x43` | `MD_AUTH_KEY` | `0` | data sheet |
| `0x44` | `PL_DEC_MTHD` / `PL_DEC_KEY` | `0` / `0` (plain; the data sheet says always 0) | data sheet |
| `0x46` | `PL_LEN` | `0x0074` (116 bytes) | data sheet field; value from Zephyr/DFP |
| `0x48` | firmware image revision | `0x01000000` | DFP script |
| `0x4C` | image source address | `0x01000200` | DFP script |
| `0x50` | image destination (run) address | `0x01000200` | DFP script |
| `0x54` | image length | computed per build (bytes) | DFP script |
| `0x58-0x1FF` | image auth/decrypt bytes, signatures | 0 (unused with auth *None*) | DFP script |

## How it gets into the build

MPLAB does not add the header, so the build does:

- [`cmake/BZ6SlotCar/default/user.cmake`](cmake/BZ6SlotCar/default/user.cmake) runs [`scripts/add_bz6_metadata.py`](scripts/add_bz6_metadata.py) after linking.
- The script adds the 512-byte header to `out/BZ6SlotCar/default.elf` as a section **and** a loadable program header. MPLAB's programmer loads from the program headers, so both are needed. The HEX is generated afterwards and includes it too.
- Sequence number, auth method and revision are CMake cache variables: `BZ6_METADATA_SEQ_NUM`, `BZ6_METADATA_AUTH_MTHD`, `BZ6_METADATA_FW_REV`.

## Checking a board

Read the word at `0x01000018`. It must be `0x4D434850`. With MDB: `x /16xwp 0x01000000`. All `0xFFFFFFFF` means the header was not programmed.
