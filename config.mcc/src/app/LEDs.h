#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * WS2812 driver — 4x4 matrix (16 LEDs) via SERCOM4 SPI (DO = PAD3 / PA9, mikroBUS 2 MOSI) - BZ6 port
 *
 * Matrix layout (physical LED index in chain):
 *   Row 3 (top)    : 12 13 14 15
 *   Row 2          :  8  9 10 11
 *   Row 1          :  4  5  6  7
 *   Row 0 (bottom) :  0  1  2  3
 */

#define LEDS_NUM_LEDS   (16U)
#define LEDS_NUM_ROWS   (4U)
#define LEDS_NUM_COLS   (4U)

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} LEDs_Color;

/* Predefined colours */
#define LEDS_COLOR_OFF       ((LEDs_Color){0U,   0U,   0U  })
#define LEDS_COLOR_RED       ((LEDs_Color){255U, 0U,   0U  })
#define LEDS_COLOR_GREEN     ((LEDs_Color){0U,   255U, 0U  })
#define LEDS_COLOR_GREEN_DIM ((LEDs_Color){0U,   30U,  0U  })
#define LEDS_COLOR_BLUE      ((LEDs_Color){0U,   0U,   255U})
#define LEDS_COLOR_BLUE_DIM  ((LEDs_Color){0U,   0U,   30U })
#define LEDS_COLOR_ORANGE    ((LEDs_Color){255U, 80U,  0U  })
#define LEDS_COLOR_YELLOW    ((LEDs_Color){255U, 255U, 0U  })
#define LEDS_COLOR_YELLOW_DIM ((LEDs_Color){40U,  30U,  0U  })
#define LEDS_COLOR_WHITE     ((LEDs_Color){255U, 255U, 255U})

void LEDs_Initialize(void);
void LEDs_SetLed(uint8_t index, LEDs_Color color);
void LEDs_SetRow(uint8_t row, LEDs_Color color);
void LEDs_SetAll(LEDs_Color color);
void LEDs_Clear(void);
void LEDs_Show(void);

/* Race start light sequence:
 *   Row 0 RED → +1 s Row 1 RED → +1 s Row 2 RED → +1 s Row 3 RED → +1 s all GREEN */
void LEDs_RaceStart(void);

/* Colour cycle test: all GREEN → all BLUE → all RED, 3 s each, repeats 'cycles' times */
void LEDs_ColorCycleTest(uint8_t cycles);

/* Blink the whole matrix in 'color' for 'durationMs', toggling every 'periodMs/2'.
 * Leaves the matrix cleared when finished. */
void LEDs_Flash(LEDs_Color color, uint16_t periodMs, uint16_t durationMs);

/* Non-blocking chase: lights one LED at a time cycling through the matrix.
 * Call periodically; advances one step each 'intervalMs' elapsed. */
void LEDs_ChaseStep(LEDs_Color color, uint16_t intervalMs);

#endif /* LEDS_H */
