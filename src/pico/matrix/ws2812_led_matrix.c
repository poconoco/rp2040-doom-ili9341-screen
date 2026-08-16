//
// See ws2812_led_matrix.h. Pixel push protocol adapted from
// https://github.com/poconoco/water-matrix (lib/WS2812/WS2812.c).
//

#include "ws2812_led_matrix.h"
#include "ws2812.pio.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/time.h"

// video_doom_pio (which normally lives on pio0) is only ever loaded/run by
// the scanvideo/ILI9341 backend, which RP2350_MATRIX builds never call --
// core1() skips scanvideo_setup() entirely in matrix mode (see i_video.c).
// So pio0 is completely unused for the whole lifetime of a matrix build,
// same as it is in water-matrix (which this pin/PIO choice matches exactly).
// pio1 is claimed by this project's I2S audio (PICO_AUDIO_I2S_PIO=1, see
// src/pico/CMakeLists.txt) regardless of video backend, so that one's out.
static PIO ws2812_pio = pio0;
static uint ws2812_sm;

typedef struct {
    uint8_t r, g, b;
} ws2812_rgb_t;

static ws2812_rgb_t leds[WS2812_MATRIX_WIDTH * WS2812_MATRIX_HEIGHT];

void ws2812_matrix_init(uint8_t pin) {
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    ws2812_sm = pio_claim_unused_sm(ws2812_pio, true);

    // Mirrors ws2812.pio's own generated ws2812_program_init() helper,
    // except the clock-divider math is done in integer/fixed-point instead
    // of float. This project builds doom_tiny/doom_tiny_nost with
    // pico_set_float_implementation(... none) (see src/CMakeLists.txt) --
    // deliberately, to catch accidental float use elsewhere -- and under
    // that setting *every* floating-point op panics, including the
    // "float div = ..." in the generated helper. That's what was silently
    // hanging boot before a single byte ever reached the matrix: nothing
    // wrong with the pin, PIO block, or DOOM engine at all.
    pio_gpio_init(ws2812_pio, pin);
    pio_sm_set_consecutive_pindirs(ws2812_pio, ws2812_sm, pin, 1, true);

    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    const uint32_t freq_hz = 800000;
    const uint32_t cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    const uint32_t denom = freq_hz * cycles_per_bit;
    uint32_t clk = clock_get_hz(clk_sys);
    uint16_t div_int = (uint16_t) (clk / denom);
    uint8_t div_frac8 = (uint8_t) (((clk % denom) * 256) / denom);
    sm_config_set_clkdiv_int_frac(&c, div_int, div_frac8);

    pio_sm_init(ws2812_pio, ws2812_sm, offset, &c);
    pio_sm_set_enabled(ws2812_pio, ws2812_sm, true);
}

static inline unsigned index_for(uint8_t x, uint8_t y) {
    return (unsigned) WS2812_MATRIX_WIDTH * x + y;
}

void ws2812_matrix_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= WS2812_MATRIX_WIDTH || y >= WS2812_MATRIX_HEIGHT)
        return;
    // Scale the whole triplet down together (preserving hue/ratio) rather
    // than clamping each channel independently -- independent clamping
    // means any color where two or more channels exceed the limit collapses
    // toward gray/white once every channel hits the same ceiling, losing
    // all color information. Every diagnostic color used elsewhere in this
    // file is pure/saturated (only 1-2 channels nonzero) so this never
    // showed up until real, mixed-channel DOOM content started flowing
    // through matrix_show_frame() and came out uniformly white.
    uint8_t max_c = r;
    if (g > max_c) max_c = g;
    if (b > max_c) max_c = b;
    ws2812_rgb_t *led = &leds[index_for(x, y)];
    if (max_c <= MATRIX_BRIGHTNESS_LIMIT || max_c == 0) {
        led->r = r;
        led->g = g;
        led->b = b;
    } else {
        led->r = (uint8_t) ((unsigned) r * MATRIX_BRIGHTNESS_LIMIT / max_c);
        led->g = (uint8_t) ((unsigned) g * MATRIX_BRIGHTNESS_LIMIT / max_c);
        led->b = (uint8_t) ((unsigned) b * MATRIX_BRIGHTNESS_LIMIT / max_c);
    }
}

void ws2812_matrix_clear(void) {
    for (unsigned i = 0; i < WS2812_MATRIX_WIDTH * WS2812_MATRIX_HEIGHT; i++) {
        leds[i].r = leds[i].g = leds[i].b = 0;
    }
}

// Empirically confirmed: calling this again with ~zero gap since the
// previous call hangs the PIO SM solid (looks like a WS2812 inter-frame
// latch/reset requirement biting harder than usual in this exact setup).
// Zero-initialized, so the very first call never waits.
static absolute_time_t next_allowed_show;

void ws2812_matrix_show(void) {
    busy_wait_until(next_allowed_show);
    for (unsigned i = 0; i < WS2812_MATRIX_WIDTH * WS2812_MATRIX_HEIGHT; i++) {
        uint32_t grb = ((uint32_t) leds[i].r << 24) | ((uint32_t) leds[i].g << 16) | ((uint32_t) leds[i].b << 8);
        pio_sm_put_blocking(ws2812_pio, ws2812_sm, grb);
    }
    next_allowed_show = make_timeout_time_ms(100);
}
