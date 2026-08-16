//
// Minimal WS2812 RGB LED matrix driver over PIO, used by the RP2350_MATRIX
// build mode in place of the ILI9341 LCD. Adapted from
// https://github.com/poconoco/water-matrix (lib/WS2812).
//
// This is a dumb pixel pusher only -- it knows nothing about DOOM's
// framebuffer or palette. That downscale/conversion happens in i_video.c,
// which calls the functions below.
//

#ifndef WS2812_LED_MATRIX_H
#define WS2812_LED_MATRIX_H

#include <stdint.h>

#define WS2812_MATRIX_WIDTH  8
#define WS2812_MATRIX_HEIGHT 8

#ifndef MATRIX_DATA_PIN
#define MATRIX_DATA_PIN 16
#endif

// Caps every channel of every pixel before it is sent, independent of
// whatever color WS2812_MATRIX_SET_PIXEL was asked to show. 64 WS2812 LEDs
// at full white draw around 3.8A, far more than USB or most 5V supplies can
// give this board -- keep this low unless the matrix has its own injected
// power. 0-255, defaults to a conservative fraction of full brightness.
#ifndef MATRIX_BRIGHTNESS_LIMIT
#define MATRIX_BRIGHTNESS_LIMIT 24
#endif

// Claims a PIO state machine and configures it to drive the matrix's data
// line on the given GPIO pin.
void ws2812_matrix_init(uint8_t pin);

// Sets one pixel in the not-yet-sent frame. (0,0) is the top-left of the
// matrix when its data-in connector faces up. Out-of-range x/y are ignored.
void ws2812_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

void ws2812_matrix_clear(void);

// Shifts the current frame out to the physical matrix. Blocks (briefly) as
// the PIO FIFO drains.
void ws2812_matrix_show(void);

#endif // WS2812_LED_MATRIX_H
