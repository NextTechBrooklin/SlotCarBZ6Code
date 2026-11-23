#include "LEDs.h"

#include <string.h>

#include "board_hal.h"
#include "os_port.h"

/*
 * WS2812 timing (MikroE datasheet):
 *   T0H = 350 ns ±150  (200–500 ns)
 *   T0L = 800 ns ±150  (650–950 ns)
 *   T1H = 700 ns ±150  (550–850 ns)
 *   T1L = 600 ns ±150  (450–750 ns)
 *   RES = >50 µs
 *
 * SERCOM1 SPI clock: 4.0 MHz (BZ6: 64 MHz GCLK, BAUD = 7)  →  T_spi = 250 ns
 * 5-bit encoding per WS2812 bit:
 *   0 bit → 11000  T0H = 2×250 = 500 ns ✓  T0L = 3×250 = 750 ns ✓
 *   1 bit → 11100  T1H = 3×250 = 750 ns ✓  T1L = 2×250 = 500 ns ✓
 *
 * Per WS2812 bit : 5 SPI bits
 * Per WS2812 byte: 8 × 5 = 40 SPI bits = 5 SPI bytes
 * Per LED        : 3 colours × 5 SPI bytes = 15 SPI bytes
 * 16 LEDs        : 240 SPI bytes of pixel data
 *
 * Reset: MOSI must be held low for >50 µs before data and after the last pixel.
 *   At 4.286 MHz, 1 byte ≈ 1.87 µs.
 *   Pre-data  (SPI_LEAD_BYTES)  : 30 bytes ≈ 56 µs ✓
 *   Post-data (SPI_RESET_BYTES) : 160 bytes ≈ 299 µs (covers WS2812B v4/v5 >280 µs) ✓
 *
 * Data order: GRB MSB first (WS2812 native).
 */

#define SPI_LEAD_BYTES      (30U)
#define SPI_BYTES_PER_COLOR (5U)
#define SPI_BYTES_PER_LED   (SPI_BYTES_PER_COLOR * 3U)    /* 15 */
#define SPI_PIXEL_BYTES     (LEDS_NUM_LEDS * SPI_BYTES_PER_LED)  /* 240 */
#define SPI_RESET_BYTES     (160U)
#define SPI_BUFFER_SIZE     (SPI_LEAD_BYTES + SPI_PIXEL_BYTES + SPI_RESET_BYTES)  /* 430 */

static LEDs_Color g_leds[LEDS_NUM_LEDS];
static uint8_t    g_spiBuf[SPI_BUFFER_SIZE];

/*
 * Encode one colour byte into 5 SPI bytes (40 SPI bits, 5 bits per WS2812 bit).
 *
 * Bit packing (MSB first, bit 7 down to bit 0):
 *   8 WS2812 bits × 5 SPI bits = 40 SPI bits = 5 bytes
 *
 * To keep the arithmetic simple we build a 64-bit word:
 *   bits [39:35] = WS2812 bit 7 encoded
 *   bits [34:30] = WS2812 bit 6 encoded
 *   ...
 *   bits [4:0]   = WS2812 bit 0 encoded
 */
static void LEDs_EncodeByte(uint8_t colorByte, uint8_t *spiBuf)
{
    uint64_t encoded = 0U;
    int8_t   bit;

    for (bit = 7; bit >= 0; bit--)
    {
        encoded <<= 5U;
        if ((colorByte & (1U << (uint8_t)bit)) != 0U)
        {
            encoded |= 0x1CU;   /* 11100 */
        }
        else
        {
            encoded |= 0x18U;   /* 11000 */
        }
    }

    spiBuf[0] = (uint8_t)((encoded >> 32U) & 0xFFU);
    spiBuf[1] = (uint8_t)((encoded >> 24U) & 0xFFU);
    spiBuf[2] = (uint8_t)((encoded >> 16U) & 0xFFU);
    spiBuf[3] = (uint8_t)((encoded >>  8U) & 0xFFU);
    spiBuf[4] = (uint8_t)(encoded           & 0xFFU);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void LEDs_Initialize(void)
{
    (void)memset(g_leds,   0, sizeof(g_leds));
    (void)memset(g_spiBuf, 0, sizeof(g_spiBuf));
    HAL_LEDSPI_Initialize();
}

void LEDs_SetLed(uint8_t index, LEDs_Color color)
{
    if (index < LEDS_NUM_LEDS)
    {
        g_leds[index] = color;
    }
}

void LEDs_SetRow(uint8_t row, LEDs_Color color)
{
    uint8_t col;
    if (row < LEDS_NUM_ROWS)
    {
        for (col = 0U; col < LEDS_NUM_COLS; col++)
        {
            g_leds[(row * LEDS_NUM_COLS) + col] = color;
        }
    }
}

void LEDs_SetAll(LEDs_Color color)
{
    uint8_t i;
    for (i = 0U; i < LEDS_NUM_LEDS; i++)
    {
        g_leds[i] = color;
    }
}

void LEDs_Clear(void)
{
    LEDs_SetAll(LEDS_COLOR_OFF);
    LEDs_Show();
}

void LEDs_Show(void)
{
    uint8_t  i;
    uint16_t offset = SPI_LEAD_BYTES;

    for (i = 0U; i < LEDS_NUM_LEDS; i++)
    {
        /* WS2812 native order: G R B */
        LEDs_EncodeByte(g_leds[i].g, &g_spiBuf[offset]);
        offset += SPI_BYTES_PER_COLOR;
        LEDs_EncodeByte(g_leds[i].r, &g_spiBuf[offset]);
        offset += SPI_BYTES_PER_COLOR;
        LEDs_EncodeByte(g_leds[i].b, &g_spiBuf[offset]);
        offset += SPI_BYTES_PER_COLOR;
    }

    /* Ensure reset region is zero */
    (void)memset(&g_spiBuf[SPI_LEAD_BYTES + SPI_PIXEL_BYTES], 0U, SPI_RESET_BYTES);

    HAL_LEDSPI_Write(g_spiBuf, SPI_BUFFER_SIZE);

    while (HAL_LEDSPI_IsBusy() == true)
    {
    }
}

/* -------------------------------------------------------------------------
 * Colour cycle test: GREEN → BLUE → RED, 3 s per colour
 * ---------------------------------------------------------------------- */
void LEDs_ColorCycleTest(uint8_t cycles)
{
    static const LEDs_Color sequence[] = {
        LEDS_COLOR_GREEN,
        LEDS_COLOR_BLUE,
        LEDS_COLOR_RED
    };
    static const uint8_t NUM_COLORS = (uint8_t)(sizeof(sequence) / sizeof(sequence[0]));

    uint8_t cycle;
    uint8_t col;

    for (cycle = 0U; cycle < cycles; cycle++)
    {
        for (col = 0U; col < NUM_COLORS; col++)
        {
            LEDs_SetAll(sequence[col]);
            LEDs_Show();
            OS_DelayMs(1000U);
        }
    }

    LEDs_Clear();
}

/* -------------------------------------------------------------------------
 * Flash the whole matrix in a colour for a fixed duration
 * ---------------------------------------------------------------------- */
void LEDs_Flash(LEDs_Color color, uint16_t periodMs, uint16_t durationMs)
{
    uint16_t halfMs   = (uint16_t)(periodMs / 2U);
    uint16_t elapsed  = 0U;
    bool     on       = true;

    if (halfMs == 0U)
    {
        halfMs = 1U;
    }

    while (elapsed < durationMs)
    {
        uint16_t step = halfMs;
        if ((uint16_t)(durationMs - elapsed) < step)
        {
            step = (uint16_t)(durationMs - elapsed);
        }

        LEDs_SetAll(on ? color : LEDS_COLOR_OFF);
        LEDs_Show();
        OS_DelayMs(step);

        on       = !on;
        elapsed += step;
    }

    LEDs_Clear();
}

/* -------------------------------------------------------------------------
 * Non-blocking chase pattern
 * ---------------------------------------------------------------------- */
static uint8_t  g_chaseIndex   = 0U;
static uint32_t g_chaseLastMs  = 0U;

void LEDs_ChaseStep(LEDs_Color color, uint16_t intervalMs)
{
    uint32_t now = OS_Millis();

    if ((now - g_chaseLastMs) < (uint32_t)intervalMs)
    {
        return;
    }
    g_chaseLastMs = now;

    LEDs_SetAll(LEDS_COLOR_OFF);
    LEDs_SetLed(g_chaseIndex, color);
    LEDs_Show();

    g_chaseIndex++;
    if (g_chaseIndex >= LEDS_NUM_LEDS)
    {
        g_chaseIndex = 0U;
    }
}

/* -------------------------------------------------------------------------
 * Race start sequence (bottom row first, 1 s between rows, all green at end)
 * ---------------------------------------------------------------------- */
void LEDs_RaceStart(void)
{
    uint8_t row;

    LEDs_Clear();

    for (row = 0U; row < LEDS_NUM_ROWS; row++)
    {
        LEDs_SetRow(row, LEDS_COLOR_RED);
        LEDs_Show();
        OS_DelayMs(1000U);
    }

    LEDs_SetAll(LEDS_COLOR_GREEN);
    LEDs_Show();
    OS_DelayMs(1000U);

    LEDs_Clear();
}
