//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM graphics stuff for Pico.
//

#if PICODOOM_RENDER_NEWHOPE
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <doom/r_data.h>
#include "doom/f_wipe.h"
#include "pico.h"

#include "config.h"
#include "d_loop.h"
#include "deh_str.h"
#include "doomtype.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_diskicon.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "picodoom.h"
//dahai
#include "video_doom.pio.h"
// #define video_doom_offset_end_of_scanline_skip_ALIGN 0u
// #define video_doom_offset_raw_run 3u
// #define video_doom_offset_raw_1p 7u

#include "image_decoder.h"
#if PICO_ON_DEVICE
#include "hardware/dma.h"
#include "hardware/structs/xip_ctrl.h"
#endif

//dahai
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "magc.h"
static int display_dma_channel;

#if RP2350_MATRIX
#include "matrix/ws2812_led_matrix.h"
#endif

#define YELLOW_SUBMARINE 0
#define SUPPORT_TEXT 1
#if SUPPORT_TEXT
typedef struct __packed {
    const char * const name;
    const uint8_t * const data;
    const uint8_t w;
    const uint8_t h;
} txt_font_t;
#define TXT_SCREEN_W 40
#include "fonts/normal.h"

static uint16_t ega_colors[] = {
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0x00, 0x00),         // 0: Black
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0x00, 0xa8),         // 1: Blue
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0xa8, 0x00),         // 2: Green
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x00, 0xa8, 0xa8),         // 3: Cyan
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0x00, 0x00),         // 4: Red
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0x00, 0xa8),         // 5: Magenta
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0x54, 0x00),         // 6: Brown
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xa8, 0xa8, 0xa8),         // 7: Grey
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0x54, 0x54),         // 8: Dark grey
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0x54, 0xfe),         // 9: Bright blue
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0xfe, 0x54),         // 10: Bright green
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x54, 0xfe, 0xfe),         // 11: Bright cyan
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0x54, 0x54),         // 12: Bright red
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0x54, 0xfe),         // 13: Bright magenta
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0xfe, 0x54),         // 14: Yellow
    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xfe, 0xfe, 0xfe),         // 15: Bright white
};
#endif

// todo temproarly turned this off because it causes a seeming bug in scanvideo (perhaps only with the new callback stuff) where the last repeated scanline of a pixel line is freed while shown
//  note it may just be that this happens anyway, but usually we are writing slower than the beam?
#define USE_INTERP PICO_ON_DEVICE
#if USE_INTERP
#include "hardware/interp.h"
#endif

CU_REGISTER_DEBUG_PINS(scanline_copy)
//CU_SELECT_DEBUG_PINS(scanline_copy)

static const patch_t *stbar;

// stbar used to be resolved eagerly in I_InitGraphics(), but that runs (see
// d_main.c) long before R_Init()/R_InitData() populate whd_vpatch_numbers,
// which resolve_vpatch_handle() depends on. On RP2040 (Cortex-M0+, no MPU)
// that premature read just landed on harmless stray memory; on RP2350
// (Cortex-M33, real MPU/SAU) it can hard-fault with no handler configured,
// hanging boot silently before a single frame renders. Resolving lazily on
// first actual use -- both call sites below run only during real frame
// rendering, well after R_Init() -- sidesteps the ordering issue entirely
// while returning the exact same value either way.
static inline const patch_t *get_stbar(void) {
    if (!stbar)
        stbar = resolve_vpatch_handle(VPATCH_STBAR);
    return stbar;
}

volatile uint8_t interp_in_use;

#define USE_1280x1024x60 1

// display has been set up?

static boolean initialized = false;

boolean screenvisible = true;

//int vga_porch_flash = false;

//static int startup_delay = 1000;

// The screen buffer; this is modified to draw things to the screen
//pixel_t *I_VideoBuffer = NULL;
// Gamma correction level to use

boolean screensaver_mode = false;

// Set to 4 for maximum in-game gamma/brightness by default
isb_int8_t usegamma = 4;

// Joystick/gamepad hysteresis
unsigned int joywait = 0;

pixel_t *I_VideoBuffer; // todo can't have this

uint8_t __aligned(4) frame_buffer[2][SCREENWIDTH*MAIN_VIEWHEIGHT];
static uint16_t palette[256];
//dahai
static uint16_t __scratch_x("shared_pal") shared_pal[NUM_SHARED_PALETTES][16];
static int8_t next_pal=-1;

semaphore_t render_frame_ready, display_frame_freed;
semaphore_t core1_launch;

uint8_t *text_screen_data;
static uint32_t *text_scanline_buffer_start;
static uint8_t *text_screen_cpy;
static uint8_t *text_font_cpy;

#if USE_1280x1024x60
//static uint32_t missing_scanline_data[] = {
//        video_doom_offset_raw_1p | (0 << 16u),
//        video_doom_offset_end_of_scanline_skip_ALIGN
//};

static uint32_t missing_scanline_data[] =
        {
#if YELLOW_SUBMARINE
                video_doom_offset_color_run | (PICO_SCANVIDEO_PIXEL_FROM_RGB8(255,255,0) << 16u),
                120 | (video_doom_offset_raw_1p << 16u),
#endif
                0u | (video_doom_offset_end_of_scanline_ALIGN << 16u)
        };

#if PICO_ON_DEVICE
bool video_doom_adapt_for_mode(const struct scanvideo_pio_program *program, const struct scanvideo_mode *mode,
                               struct scanvideo_scanline_buffer *missing_scanvideo_scanline_buffer, uint16_t *modifiable_instructions);
pio_sm_config video_doom_configure_pio(pio_hw_t *pio, uint sm, uint offset);
#endif
#define VIDEO_DOOM_PROGRAM_NAME "doom"
const struct scanvideo_pio_program video_doom_pio = {
#if PICO_ON_DEVICE
        .program = &video_doom_program,
        .adapt_for_mode = video_doom_adapt_for_mode,
        .configure_pio = video_doom_configure_pio,
#else
        .id = VIDEO_DOOM_PROGRAM_NAME
#endif
};

const scanvideo_timing_t vga_timing_1280x1000_60_default = // same as 1280x1024_60 standard just with some 12 blank lines at the top and bottom
        {
                .clock_freq = 108000000,

                .h_active = 1280,
                .v_active = 1024 - 24,

                .h_front_porch = 48,
                .h_pulse = 112,
                .h_total = 1688,
                .h_sync_polarity = 0,

                .v_front_porch = 1 + 24 - 12, // center our slightly short screen
                .v_pulse = 3,
                .v_total = 1066,
                .v_sync_polarity = 0,
        };

const scanvideo_timing_t vga_timing_640x1000_60_default = // same as 1280x1024_60 standard just with some 12 blank lines at the top and bottom
        {
                .clock_freq = 108000000 / 2,

#if PICO_ON_DEVICE
                .h_active = 1280 / 2,
#else
                .h_active = 1280,
#endif
                .v_active = 1024 - 24,

                .h_front_porch = 48 / 2,
                .h_pulse = 112 / 2,
                .h_total = 1688 / 2,
                .h_sync_polarity = 0,

                .v_front_porch = 1 + 24 - 12, // center our slightly short screen
                .v_pulse = 3,
                .v_total = 1066,
                .v_sync_polarity = 0,
        };
/*
 *dahai
 */
const scanvideo_timing_t vga_timing_320x240_60_lcd =
        {
            #ifdef ILI9341
                .clock_freq = 108000000 / 8,
                .h_active = 1280 / 2,
                .v_active = 1000 / 1,

                .h_front_porch = 48 / 2,
                .h_pulse = 112 / 2,
                .h_total = 1688 / 2,
                .h_sync_polarity = 0,

                .v_front_porch = 1 + 24 - 12,
                .v_pulse = 3,
                .v_total = 1066 / 1,
                .v_sync_polarity = 0,
            #endif
            #ifdef ST7789
                .clock_freq = 108000000 / 8,
                .h_active = 1280 / 2 ,
                .v_active = 1000 / 1 ,

                .h_front_porch = 48 / 2 ,
                .h_pulse = 112 / 2 ,
                .h_total = 1688 / 2 ,
                .h_sync_polarity = 0,

                .v_front_porch = 1 + 24 - 12,
                .v_pulse = 3,
                .v_total = 1066 / 1 ,
                .v_sync_polarity = 0,
            #endif

                // .enable_clock = 0,
                // .clock_polarity = 0,

                // .enable_den = 0
        };

const scanvideo_mode_t vga_mode_320x200 =
        {
            //dahai
                .default_timing = &vga_timing_320x240_60_lcd,
                // .default_timing = &vga_timing_640x1000_60_default,
                .pio_program = &video_doom_pio,
#if PICO_ON_DEVICE
                #ifdef ILI9341
                .width = 320,
                #endif
                #ifdef ST7789
                .width = 160,
                #endif
#else
                .width = 640,
#endif
                //dahai
                #ifdef ILI9341
                .height = 200,

                .xscale = 2,
                .yscale = 5,
                #endif
                #ifdef ST7789
                .xscale = 2,
                .yscale = 5,

                .height = 200,
                #endif
                // .xscale = 2,
                // .yscale = 5,
        };



#define VGA_MODE vga_mode_320x200
#elif USE_320x240x60
#define VGA_MODE vga_mode_320x240_60
#else
const scanvideo_mode_t vga_mode_320x200_60 =
        {
                .default_timing = &vga_timing_1280x1024_60_default,
                .pio_program = &video_24mhz_composable,
                .width = 320,
                .height = 204,
                .xscale = 4,
                .yscale = 5,
        };

#define VGA_MODE vga_mode_320x200_60
#endif

#if USE_INTERP
static interp_hw_save_t interp0_save, interp1_save;
static boolean interp_updated;
static boolean need_save;

static inline void interp_save_static(interp_hw_t *interp, interp_hw_save_t *saver) {
    saver->accum[0] = interp->accum[0];
    saver->accum[1] = interp->accum[1];
    saver->base[0] = interp->base[0];
    saver->base[1] = interp->base[1];
    saver->base[2] = interp->base[2];
    saver->ctrl[0] = interp->ctrl[0];
    saver->ctrl[1] = interp->ctrl[1];
}

static inline void interp_restore_static(interp_hw_t *interp, interp_hw_save_t *saver) {
    interp->accum[0] = saver->accum[0];
    interp->accum[1] = saver->accum[1];
    interp->base[0] = saver->base[0];
    interp->base[1] = saver->base[1];
    interp->base[2] = saver->base[2];
    interp->ctrl[0] = saver->ctrl[0];
    interp->ctrl[1] = saver->ctrl[1];
}
#endif

void I_ShutdownGraphics(void)
{
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?
}

//
// Set the window title
//

void I_SetWindowTitle(const char *title)
{
//    window_title = title;
}

//
// I_SetPalette
//
void I_SetPaletteNum(int doompalette)
{
    next_pal = doompalette;
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
}

uint8_t display_frame_index;
uint8_t display_overlay_index;
uint8_t display_video_type;
#if RP2350_MATRIX
// Bumped every time new_frame_stuff() actually consumes a newly-rendered
// frame -- unlike display_frame_index, this changes even when the render
// is single-buffered (VIDEO_TYPE_SINGLE, e.g. the title screen, which is
// everything outside GS_LEVEL) and keeps redrawing into the *same* index
// every time. Gating the matrix update on display_frame_index alone means
// it only ever notices the very first frame ever rendered and then never
// again outside real gameplay -- confirmed on hardware (checkpoints loop
// forever, proving real per-frame progress, while the matrix stays frozen
// on frame 1 forever).
static volatile uint32_t matrix_frame_serial;
#endif

typedef void (*scanline_func)(uint32_t *dest, int scanline);

static void scanline_func_none(uint32_t *dest, int scanline);
static void scanline_func_double(uint32_t *dest, int scanline);
static void scanline_func_single(uint32_t *dest, int scanline);
static void scanline_func_wipe(uint32_t *dest, int scanline);

scanline_func scanline_funcs[] = {
        scanline_func_none,     // VIDEO_TYPE_NONE
        NULL,                   // VIDEO_TYPE_TEXT
        scanline_func_single,   // VIDEO_TYPE_SAVING
        scanline_func_double,   // VIDEO_TYPE_DOUBLE
        scanline_func_single,   // VIDEO_TYPE_SINGLE
        scanline_func_wipe,     // VIDEO_TYPE_WIPE
};

uint8_t *wipe_yoffsets; // position of start of y in each column
int16_t *wipe_yoffsets_raw;
uint32_t *wipe_linelookup; // offset of each line from start of screenbuffer (can be negative for FB 1 to FB 0)
uint8_t next_video_type;
uint8_t next_frame_index; // todo combine with video type?
uint8_t next_overlay_index;
#if !DEMO1_ONLY
uint8_t *next_video_scroll;
uint8_t *video_scroll;
#endif
volatile uint8_t wipe_min;
uint32_t *saved_scanline_buffer_ptrs[PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT];

#pragma GCC push_options
#if PICO_ON_DEVICE
#pragma GCC optimize("O3")
#endif

static inline void palette_convert_scanline(uint32_t *dest, const uint8_t *src) {
#if USE_INTERP
    if (interp_updated != 1) {
                if (need_save) {
                    interp_save_static(interp0, &interp0_save);
                    interp_save_static(interp1, &interp1_save);
                }
                interp_config c = interp_default_config();
                interp_config_set_shift(&c, 0);
                interp_config_set_mask(&c, 0, 7);
                interp_set_config(interp0, 0, &c);
                interp_config_set_shift(&c, 16);
                interp_set_config(interp1, 0, &c);
                interp_config_set_shift(&c, 8);
                interp_config_set_cross_input(&c, true);
                interp_set_config(interp0, 1, &c);
                interp_config_set_shift(&c, 24);
                interp_set_config(interp1, 1, &c);
                uint32_t palette_div2 = ((uintptr_t)palette) >> 1;
                interp0->base[0] = palette_div2;
                interp0->base[1] = palette_div2;
                interp1->base[0] = palette_div2;
                interp1->base[1] = palette_div2;
                interp_updated = 1;
            }
            extern void palette8to16(uint32_t *dest, const uint8_t *src, uint words);
            palette8to16(dest, src, SCREENWIDTH);
//            dest[4] = (255-scanline) * 0x2000;
            dest += SCREENWIDTH / 2;
//            dest[-4] = (255-scanline) * 0x10001;
#else
    for (int i = 0; i < SCREENWIDTH; i += 2) {
        uint32_t val = palette[*src++];
        val |= (palette[*src++]) << 16;
        *dest++ = val;
    }
#endif
}
static void scanline_func_none(uint32_t *dest, int scanline) {
    memset(dest, 0, SCREENWIDTH * 2);
}

#if SUPPORT_TEXT
void check_text_buffer(scanvideo_scanline_buffer_t *buffer) {
#if PICO_ON_DEVICE
    if (buffer->data < text_scanline_buffer_start || buffer->data >= text_scanline_buffer_start + TEXT_SCANLINE_BUFFER_TOTAL_WORDS) {
        // is an original scanvideo allocated buffer, we need to use a larger one
        int i;
        for(i=0;i<PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT;i++) {
            if (!saved_scanline_buffer_ptrs[i]) break;
        }
        assert(i<PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT);
        saved_scanline_buffer_ptrs[i] = buffer->data;
        buffer->data = text_scanline_buffer_start + i * TEXT_SCANLINE_BUFFER_WORDS;
    }
#endif
}

static void finish_text_buffer(scanvideo_scanline_buffer_t *buffer) {
    uint16_t * p = (uint16_t *)buffer->data;
    p[0] = video_doom_offset_raw_run_half;
    p[1] = p[2];
    p[2] = SCREENWIDTH - 3; 
    buffer->data[SCREENWIDTH/2 + 1] = video_doom_offset_raw_1p;
    buffer->data[SCREENWIDTH/2 + 2] = video_doom_offset_end_of_scanline_skip_ALIGN;
    buffer->data_used = SCREENWIDTH/2 + 3;
}

static void __not_in_flash_func(render_text_mode_half_scanline)(scanvideo_scanline_buffer_t *buffer, const uint8_t *text_data, int yoffset) {
    uint16_t * p = (uint16_t *)(buffer->data + 1);
//    memset(buffer->data + 1, 0, 1280);
//    uint x = scanline * 2 + yoffset;
//    buffer->data[1 + (x/2)] = x&1 ? 0xffff0000 : 0xffff;
#if 1
    uint blink = scanvideo_frame_number(buffer->scanline_id) & 16;
    // not going to change so just hard code
//    assert(normal_font.w == 8);
//    assert(normal_font.h == 16);
    const uint8_t *font_base = text_font_cpy + yoffset;

for(uint i=0; i<TXT_SCREEN_W; i++) {
        uint fg = text_data[1] & 0xf;
        uint bg = (text_data[1] >> 4) & 0xf;
        if (bg & 0x8) {
            bg &= ~0x8;
            // blinking
            if (blink) fg = bg;
        }
        // probably user error but this wasn't working correctly with the inline asm on the stack
        static uint16_t colors[2];
        colors[0] = ega_colors[bg];
        colors[1] = ega_colors[fg];
        uint bits8 = font_base[text_data[0] * 16];
#if PICO_ON_DEVICE
        // todo use interpolator?
        uint tmp1, tmp2, tmp3;
        __asm__ volatile (
            ".syntax unified\n"

            "movs %[r_tmp3], #2\n"
            "lsls %[r_tmp1],%[r_bits8],#1\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "movs %[r_tmp2],%[r_bits8]\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

            "lsrs %[r_tmp1],%[r_bits8],#1\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "lsrs %[r_tmp2],%[r_bits8],#2\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

            "lsrs %[r_tmp1],%[r_bits8],#3\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "lsrs %[r_tmp2],%[r_bits8],#4\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

            "lsrs %[r_tmp1],%[r_bits8],#5\n"
            "ands %[r_tmp1],%[r_tmp3]\n"
            "ldrh %[r_tmp1],[%[r_colors],%[r_tmp1]]\n"

            "lsrs %[r_tmp2],%[r_bits8],#6\n"
            "ands %[r_tmp2],%[r_tmp3]\n"
            "ldrh %[r_tmp2],[%[r_colors],%[r_tmp2]]\n"

            "lsls %[r_tmp2], #16\n"
            "orrs %[r_tmp1], %[r_tmp2]\n"
            "stmia %[r_p]!, {%[r_tmp1]}\n"

        : [ r_p] "+l" (p),
              [ r_tmp1] "=&l" (tmp1),
              [ r_tmp2] "=&l" (tmp2),
              [ r_tmp3] "=&l" (tmp3)

            : [ r_bits8] "l" (bits8),
              [ r_colors] "l" (colors)
            :
        );
#else
        p[0] = colors[bits8&1];
        p[1] = colors[(bits8>>1)&1];
        p[2] = colors[(bits8>>2)&1];
        p[3] = colors[(bits8>>3)&1];
        p[4] = colors[(bits8>>4)&1];
        p[5] = colors[(bits8>>5)&1];
        p[6] = colors[(bits8>>6)&1];
        p[7] = colors[(bits8>>7)&1];
        p+=8;
#endif
        text_data+=2;
    }
#endif
}

static void __noinline render_text_mode_scanline(scanvideo_scanline_buffer_t *buffer, int scanline) {
    const uint8_t *text_data = text_screen_data;
    assert(text_data);
    
    // 1. Advance the text row every 16 scanlines (instead of 8)
    text_data += TXT_SCREEN_W * 2 * (scanline / 16);
    check_text_buffer(buffer);
    
    // 2. Fetch the exact pixel row of the font (0 through 15) without skipping
    render_text_mode_half_scanline(buffer, text_data, scanline % 16);
    finish_text_buffer(buffer);
    
    if (buffer->link) {
        buffer->link_after = 2;
        buffer->link->link_after = 0;
        check_text_buffer(buffer->link);
        
        // 3. Ensure the linked buffer also doesn't skip lines
        render_text_mode_half_scanline(buffer->link, text_data, scanline % 16);
        finish_text_buffer(buffer->link);
    }
}
#endif

static void __scratch_x("scanlines") scanline_func_double(uint32_t *dest, int scanline) {
    if (scanline < MAIN_VIEWHEIGHT) {
        const uint8_t *src = frame_buffer[display_frame_index] + scanline * SCREENWIDTH;
//        if (scanline == 100) {
//            printf("SL %d %p\n", display_frame_index, &frame_buffer[display_frame_index]);
//        }
        palette_convert_scanline(dest, src);
    } else {
        // we expect everything to be overdrawn by statusbar so we do nothing
    }
}

static void __not_in_flash_func(scanline_func_single)(uint32_t *dest, int scanline) {
    uint8_t *src;
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index] + scanline * SCREENWIDTH;
    } else {
        src = frame_buffer[display_frame_index^1] + (scanline - 32) * SCREENWIDTH;
    }
#if !DEMO1_ONLY
    if (video_scroll) {
        for(int i=SCREENWIDTH-1;i>0;i--) {
            src[i] = src[i-1];
        }
        src[0] = video_scroll[scanline];
    }
#endif
    palette_convert_scanline(dest, src);
}

static void scanline_func_wipe(uint32_t *dest, int scanline) {
    const uint8_t *src;
#if 0
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index^1] + scanline * SCREENWIDTH;
    } else {
        src = frame_buffer[display_frame_index] + (scanline - 32) * SCREENWIDTH;
    }
    palette_convert_scanline(dest, src);
    return;
#endif
    if (scanline < MAIN_VIEWHEIGHT) {
        src = frame_buffer[display_frame_index];
    } else {
        src = frame_buffer[display_frame_index^1] - 32 * SCREENWIDTH;
    }
    assert(wipe_yoffsets && wipe_linelookup);
    uint16_t *d = (uint16_t *)dest;
    src += scanline * SCREENWIDTH;
    for (int i = 0; i < SCREENWIDTH; i++) {
        int rel = scanline - wipe_yoffsets[i];
        if (rel < 0) {
            d[i] = palette[src[i]];
        } else {
            const uint8_t *flip;
#if PICO_ON_DEVICE
            flip = (const uint8_t *)wipe_linelookup[rel];
#else
            flip = &frame_buffer[0][0] + wipe_linelookup[rel];
#endif
            // todo better protection here
            if (flip >= &frame_buffer[0][0] && flip < &frame_buffer[0][0] + 2 * SCREENWIDTH * MAIN_VIEWHEIGHT) {
                d[i] = palette[flip[i]];
            }
        }
    }
}

static inline uint draw_vpatch(uint16_t *dest, patch_t *patch, vpatchlist_t *vp, uint off) {
    int repeat = vp->entry.repeat;
    dest += vp->entry.x;
    int w = vpatch_width(patch);
    const uint8_t *data0 = vpatch_data(patch);
    const uint8_t *data = data0 + off;
    if (!vpatch_has_shared_palette(patch)) {
        const uint8_t *pal = vpatch_palette(patch);
        switch (vpatch_type(patch)) {
            case vp4_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 1; i < len; i += 2) {
                        uint v = *data++;
                        *p++ = palette[pal[v & 0xf]];
                        *p++ = palette[pal[v >> 4]];
                    }
                    if (len & 1) {
                        *p++ = palette[pal[(*data++) & 0xf]];
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp4_alpha: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = palette[pal[v & 0xf]];
                    if (v >> 4) p[1] = palette[pal[v >> 4]];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = palette[pal[v & 0xf]];
                }
                break;
            }
            case vp4_solid: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    p[0] = palette[pal[v & 0xf]];
                    p[1] = palette[pal[v >> 4]];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    p[0] = palette[pal[v & 0xf]];
                }
                break;
            }
            case vp6_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 3; i < len; i += 4) {
                        uint v = *data++;
                        v |= (*data++) << 8;
                        v |= (*data++) << 16;
                        *p++ = palette[pal[v & 0x3f]];
                        *p++ = palette[pal[(v >> 6) & 0x3f]];
                        *p++ = palette[pal[(v >> 12) & 0x3f]];
                        *p++ = palette[pal[(v >> 18) & 0x3f]];
                    }
                    len &= 3;
                    if (len--) {
                        uint v = *data++;
                        *p++ = palette[pal[v & 0x3f]];
                        if (len--) {
                            v >>= 6;
                            v |= (*data++) << 2;
                            *p++ = palette[pal[v & 0x3f]];
                            if (len--) {
                                v >>= 6;
                                v |= (*data++) << 4;
                                *p++ = palette[pal[v & 0x3f]];
                                assert(!len);
                            }
                        }
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp8_runs: {
                uint16_t *p = dest;
                uint16_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 0; i < len; i++) {
                        *p++ = palette[pal[*data++]];
                    }
                    assert(p <= pend);
                    if (p == pend) break;
                }
                break;
            }
            case vp_border: {
                dest[0] = palette[*data++];
                uint16_t col = palette[*data++];
                for (int i = 1; i < w - 1; i++) dest[i] = col;
                dest[w-1] = palette[*data++];
                break;
            }
            default:
                assert(false);
                break;
        }
    } else {
        uint sp = vpatch_shared_palette(patch);
        uint16_t *pal16 = shared_pal[sp];
        assert(sp < NUM_SHARED_PALETTES);
        switch (vpatch_type(patch)) {
            case vp4_solid: {
#if PICO_ON_DEVICE
                if (patch == get_stbar()) {
                    static const uint8_t *cached_data;
                    static uint32_t __scratch_x("data_cache") data_cache[41];
                    int i = 0;
                    uint32_t *d = (uint32_t *) dest;
#define DMA_CHANNEL 11
                    if (cached_data == data) {
                        const uint8_t *source = (const uint8_t *) data_cache;
                        // we need to correct for the misalignment of data, because the XIP copy ignores the low 2 bits...
                        // the raw bitmap data is always misaligned by 3 (the size of the header in the case of stbar)
                        source += 3;
                        for (; source < (const uint8_t *) dma_hw->ch[DMA_CHANNEL].al1_write_addr; source++) {
                            uint32_t val = pal16[source[0] & 0xf];
                            val |= (pal16[source[0] >> 4]) << 16;
                            *d++ = val;
                        }
                        source -= 3;
                        i = (source - (const uint8_t *) data_cache);
                    }
                    if (true) {
                        //                        once = true;
                        xip_ctrl_hw->stream_ctr = 0;
                        // workaround yucky bug
                        (void) *(io_rw_32 *) XIP_NOCACHE_NOALLOC_BASE;
                        xip_ctrl_hw->stream_fifo;
                        dma_channel_abort(DMA_CHANNEL);
                        dma_channel_config c = dma_channel_get_default_config(DMA_CHANNEL);
                        channel_config_set_read_increment(&c, false);
                        channel_config_set_write_increment(&c, true);
                        channel_config_set_dreq(&c, DREQ_XIP_STREAM);
                        dma_channel_set_read_addr(DMA_CHANNEL, (void *) XIP_AUX_BASE, false);
                        dma_channel_set_config(DMA_CHANNEL, &c, false);
                        cached_data = data + SCREENWIDTH / 2;
                        xip_ctrl_hw->stream_addr = (uintptr_t) cached_data;
                        xip_ctrl_hw->stream_ctr = 41;
                        __compiler_memory_barrier();
                        dma_channel_transfer_to_buffer_now(DMA_CHANNEL, data_cache, 41);
                    }
                    for (; i < SCREENWIDTH / 2; i++) {
                        uint32_t val = pal16[data[i] & 0xf];
                        val |= (pal16[data[i] >> 4]) << 16;
                        *d++ = val;
                    }
                    data += SCREENWIDTH / 2;
                    break; // early break from switch
                }
#endif
                if (((uintptr_t)dest)&3) {
                    uint16_t *p = dest;
                    for (int i = 0; i < w / 2; i++) {
                        uint v = *data++;
                        p[0] = pal16[v & 0xf];
                        p[1] = pal16[v >> 4];
                        p += 2;
                    }
                } else {
                    uint32_t *wide = (uint32_t *) dest;
                    for (int i = 0; i < w / 2; i++) {
                        uint v = *data++;
                        wide[i] = pal16[v & 0xf] | (pal16[v >> 4] << 16);
                    }
                }
                if (w & 1) {
                    uint v = *data++;
                    dest[w-1] = pal16[v & 0xf];
                }
                break;
            }
            case vp4_alpha: {
                uint16_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = pal16[v & 0xf];
                    if (v >> 4) p[1] = pal16[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    uint v = *data++;
                    if (v & 0xf) p[0] = pal16[v & 0xf];
                }
                break;
            }
            default:
                assert(false);
        }
    }
    if (repeat) {
        // we need them to be solid... which they are, but if not you'll just get some visual funk
        //assert(vpatch_type(patch) == vp4_solid);
        if (vp->entry.patch_handle == VPATCH_M_THERMM) w--; // hackity hack
        for(int i=0;i<repeat*w;i++) {
            dest[w+i] = dest[i];
        }
    }
    return data - data0;
}

// this is not in flash as quite large and only once per frame
void __noinline new_frame_init_overlays_palette_and_wipe() {
    // re-initialize our overlay drawing
    if (display_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS) {
        memset(vpatchlists->vpatch_next, 0, sizeof(vpatchlists->vpatch_next));
        memset(vpatchlists->vpatch_starters, 0, sizeof(vpatchlists->vpatch_starters));
        memset(vpatchlists->vpatch_doff, 0, sizeof(vpatchlists->vpatch_doff));
        vpatchlist_t *overlays = vpatchlists->overlays[display_overlay_index];
        // do it in reverse so our linked lists are in ascending order
        for (int i = overlays->header.size - 1; i > 0; i--) {
            assert(overlays[i].entry.y < count_of(vpatchlists->vpatch_starters));
            vpatchlists->vpatch_next[i] = vpatchlists->vpatch_starters[overlays[i].entry.y];
            vpatchlists->vpatch_starters[overlays[i].entry.y] = i;
        }
        if (next_pal != -1) {
            static const uint8_t *playpal;
            static bool calculate_palettes;
            if (!playpal) {
                lumpindex_t l = W_GetNumForName("PLAYPAL");
                playpal = W_CacheLumpNum(l, PU_STATIC);
                calculate_palettes = W_LumpLength(l) == 768;
            }
            if (!calculate_palettes || !next_pal) {
                const uint8_t *doompalette = playpal + next_pal * 768;
                for (int i = 0; i < 256; i++) {
                    int r = *doompalette++;
                    int g = *doompalette++;
                    int b = *doompalette++;
                    if (usegamma) {
                        r = gammatable[usegamma-1][r];
                        g = gammatable[usegamma-1][g];
                        b = gammatable[usegamma-1][b];
                    }
                    palette[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b);
                }
            } else {
                int mul, r0, g0, b0;
                if (next_pal < 9) {
                    mul = next_pal * 65536 / 9;
                    r0 = 255; g0 = b0 = 0;
                } else if (next_pal < 13) {
                    mul = (next_pal - 8) * 65536 / 8;
                    r0 = 215; g0 = 186; b0 = 69;
                } else {
                    mul = 65536 / 8;
                    r0 = b0 = 0; g0 = 256;
                }
                const uint8_t *doompalette = playpal;
                for (int i = 0; i < 256; i++) {
                    int r = *doompalette++;
                    int g = *doompalette++;
                    int b = *doompalette++;
                    r += ((r0 - r) * mul) >> 16;
                    g += ((g0 - g) * mul) >> 16;
                    b += ((b0 - b) * mul) >> 16;
                    palette[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b);
                }
            }
            next_pal = -1;
            assert(vpatch_type(get_stbar()) == vp4_solid); // no transparent, no runs, 4 bpp
            for (int i = 0; i < NUM_SHARED_PALETTES; i++) {
                patch_t *patch = resolve_vpatch_handle(vpatch_for_shared_palette[i]);
                assert(vpatch_colorcount(patch) <= 16);
                assert(vpatch_has_shared_palette(patch));
                for (int j = 0; j < 16; j++) {
                    shared_pal[i][j] = palette[vpatch_palette(patch)[j]];
                }
            }
        }
        if (display_video_type == VIDEO_TYPE_WIPE) {
//            printf("WIPEMIN %d\n", wipe_min);
            if (wipe_min <= 200) {
                bool regular = display_overlay_index; // just happens to toggle every frame
                int new_wipe_min = 200;
                for (int i = 0; i < SCREENWIDTH; i++) {
                    int v;
                    if (wipe_yoffsets_raw[i] < 0) {
                        if (regular) {
                            wipe_yoffsets_raw[i]++;
                        }
                        v = 0;
                    } else {
                        int dy = (wipe_yoffsets_raw[i] < 16) ? (1 + wipe_yoffsets_raw[i] + regular) / 2 : 4;
                        if (wipe_yoffsets_raw[i] + dy > 200) {
                            v = 200;
                        } else {
                            wipe_yoffsets_raw[i] += dy;
                            v = wipe_yoffsets_raw[i];
                        }
                    }
                    wipe_yoffsets[i] = v;
                    if (v < new_wipe_min) new_wipe_min = v;
                }
                assert(new_wipe_min >= wipe_min);
                wipe_min = new_wipe_min;
            }
        }
    }
}

// this method moved out of scratchx because we didn't have quite enough space for core1 stack
void __no_inline_not_in_flash_func(new_frame_stuff)() {
    // this part of the per frame code is in RAM as it is needed during save
    if (sem_available(&render_frame_ready)) {
        sem_acquire_blocking(&render_frame_ready);
        display_video_type = next_video_type;
        display_frame_index = next_frame_index;
        display_overlay_index = next_overlay_index;
#if RP2350_MATRIX
        matrix_frame_serial++;
#endif
#if !DEMO1_ONLY
        video_scroll = next_video_scroll; // todo does this waste too much space
#endif
        sem_release(&display_frame_freed);
    } else {
#if !DEMO1_ONLY
        video_scroll = NULL;
#endif
    }
    if (display_video_type != VIDEO_TYPE_SAVING) {
        // this stuff is large (so in flash) and not needed in save move
        new_frame_init_overlays_palette_and_wipe();
    }
}

#if RP2350_MATRIX
// Permanent safety net, not just a bring-up aid: if anything ever crashes
// on this build (RP2350's MPU/SAU makes real hard faults far more likely
// than they were on RP2040's fault-lenient Cortex-M0+ -- see the
// whd_vpatch_numbers/get_stbar() story above for a real example), this
// decodes the CPU's own faulting PC (captured by hardware on the exception
// stack frame) into 16 two-bit color flashes (MSB first): red=00 green=01
// blue=10 yellow=11.
// Concatenating them gives the 32-bit address; look it up in the .elf
// (e.g. `arm-none-eabi-addr2line -e doom_tiny_nost.elf <addr>`) to find
// the exact crashing instruction.
static void matrix_solid(uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < WS2812_MATRIX_HEIGHT; y++)
        for (int x = 0; x < WS2812_MATRIX_WIDTH; x++)
            ws2812_matrix_set_pixel(x, y, r, g, b);
    ws2812_matrix_show();
}

// MATRIX_CHECKPOINT_REQUEST -- temporary, re-diagnosing D_DoomMain's
// startup safely this time. Earlier checkpoints called the matrix driver
// directly from core0 (D_DoomMain/R_Init) while core1's own loop was
// already calling it too -- an unsynchronized race on shared driver state
// (confirmed by inconsistent, partially-lit output across identical
// reboots). Core1 is the *only* thing allowed to touch the matrix during
// real operation; this just lets core0 request a flash that core1 performs
// on its own next iteration, so there's never two callers at once.
//
// A single overwritable slot turned out not to be enough: core0 can race
// through several checkpoints in the time core1 takes to display just one
// (800ms), silently dropping all but the last -- we were seeing a random
// sample of the real sequence, not the sequence itself. This is a small
// non-blocking ring buffer instead, so every checkpoint core0 fires gets
// displayed, in order, eventually. Not blocking on a full queue is
// deliberate: making core0 wait for core1 risks deadlock if core1 is ever
// stuck elsewhere (e.g. pd_core1_loop()'s wait for core1_wake, which only
// core0's *own* renderer releases) -- better to drop the odd checkpoint
// under overflow than freeze the very thing we're trying to observe.
// Colors turned out unreliable to read back (LED brightness/auto-exposure
// wash out subtle hue differences) -- this now shows a plain lit-pixel
// COUNT instead, always the same color, filled row-major (so e.g. N=19
// reads as "two full rows plus three pixels" = 16+3). Numbers are
// unambiguous no matter how the camera/eye is adapting.
// Master switch: the core0<->core1 render handshake is now confirmed to
// complete every frame (checkpoints reliably reach 18 and loop). Checkpoints
// fire every frame from here on out (D_Display/D_RunFrame/pd_core1_loop all
// run every tic) and each display eats ~1s and the matrix hardware itself --
// leaving them on would starve real gameplay frames of any chance to show.
// Flip back to 1 to resume step-by-step diagnosis.
#define MATRIX_CHECKPOINTS_ENABLED 0

#define MATRIX_CHECKPOINT_QUEUE_LEN 32
static volatile int32_t matrix_checkpoint_queue[MATRIX_CHECKPOINT_QUEUE_LEN]; // -1 = empty slot
static volatile uint32_t matrix_checkpoint_write = 0;
static uint32_t matrix_checkpoint_read = 0; // core1-only, not shared

void matrix_request_checkpoint(uint8_t n) {
#if !MATRIX_CHECKPOINTS_ENABLED
    return;
#endif
    uint32_t next = (matrix_checkpoint_write + 1) % MATRIX_CHECKPOINT_QUEUE_LEN;
    if (next == matrix_checkpoint_read)
        return; // queue full, drop rather than block or overwrite
    matrix_checkpoint_queue[matrix_checkpoint_write] = n;
    matrix_checkpoint_write = next;
}

// Non-blocking count display: single ws2812_matrix_show() call, no sleep_ms
// at all (unlike the flash-then-clear approach this replaced). Overwrites
// the matrix in place -- safe to
// call from inside core1's tightly-timed render loop without perturbing
// pd_core1_loop()'s handshake cadence. Stays visible until the next call
// (diagnostic or real frame) overwrites it -- if the loop hangs right after
// one of these, whatever it last painted just stays frozen on the matrix,
// which is exactly what we want: a freeze-frame of the last checkpoint
// reached, with no risk of the diagnostic itself being what caused a hang.
static void matrix_display_count_instant(int32_t n, uint8_t use_r, uint8_t use_g, uint8_t use_b) {
    if (n > WS2812_MATRIX_WIDTH * WS2812_MATRIX_HEIGHT)
        n = WS2812_MATRIX_WIDTH * WS2812_MATRIX_HEIGHT;
    for (int i = 0; i < WS2812_MATRIX_WIDTH * WS2812_MATRIX_HEIGHT; i++) {
        int x = i % WS2812_MATRIX_WIDTH, y = i / WS2812_MATRIX_WIDTH;
        uint8_t on = (i < n) ? MATRIX_BRIGHTNESS_LIMIT : 0;
        ws2812_matrix_set_pixel(x, y, use_r ? on : 0, use_g ? on : 0, use_b ? on : 0);
    }
    ws2812_matrix_show();
}

// Called only from core1's own loop. Red: core0-originated checkpoints
// (1-11, queued via matrix_request_checkpoint()).
static void matrix_service_checkpoint_request(void) {
    if (matrix_checkpoint_read == matrix_checkpoint_write)
        return; // empty
    int32_t n = matrix_checkpoint_queue[matrix_checkpoint_read];
    matrix_checkpoint_read = (matrix_checkpoint_read + 1) % MATRIX_CHECKPOINT_QUEUE_LEN;
    matrix_display_count_instant(n, 1, 0, 0);
}

// For use from *inside* pd_core1_loop() (pd_render.cpp), i.e. from code that
// already runs on core1 itself -- displays immediately, synchronously,
// bypassing the cross-core queue entirely (no race: core1 already
// exclusively owns the matrix driver). Blue: core1-internal checkpoints
// (12-18, pd_core1_loop()'s own handshake stages) -- distinct from the red
// core0 ones so we can tell which side froze. Non-blocking now (previously
// used the ~1s flash-then-clear version, which may have been accidentally
// masking a real hang by pacing the loop -- see conversation).
void matrix_checkpoint_now(uint8_t n) {
#if !MATRIX_CHECKPOINTS_ENABLED
    return;
#endif
    matrix_display_count_instant(n, 0, 0, 1);
}

void __attribute__((noreturn)) hardfault_blink(uint32_t *stacked_regs) {
    // Exception stack frame layout (pushed by hardware): r0, r1, r2, r3,
    // r12, lr, pc, xpsr.
    uint32_t pc = stacked_regs[6];
    // Re-init from scratch in case whatever faulted also clobbered our
    // driver's static state.
    ws2812_matrix_init(MATRIX_DATA_PIN);
    while (1) {
        matrix_solid(0, 0, 0);
        sleep_ms(600);
        for (int i = 0; i < 3; i++) { // 3 white flashes = "starting PC dump"
            matrix_solid(MATRIX_BRIGHTNESS_LIMIT, MATRIX_BRIGHTNESS_LIMIT, MATRIX_BRIGHTNESS_LIMIT);
            sleep_ms(200);
            matrix_solid(0, 0, 0);
            sleep_ms(200);
        }
        sleep_ms(600);
        for (int dibit = 15; dibit >= 0; dibit--) {
            uint8_t v = (pc >> (dibit * 2)) & 0x3;
            switch (v) {
                case 0: matrix_solid(MATRIX_BRIGHTNESS_LIMIT, 0, 0); break; // red = 00
                case 1: matrix_solid(0, MATRIX_BRIGHTNESS_LIMIT, 0); break; // green = 01
                case 2: matrix_solid(0, 0, MATRIX_BRIGHTNESS_LIMIT); break; // blue = 10
                case 3: matrix_solid(MATRIX_BRIGHTNESS_LIMIT, MATRIX_BRIGHTNESS_LIMIT, 0); break; // yellow = 11
            }
            sleep_ms(500);
            matrix_solid(0, 0, 0);
            sleep_ms(250);
        }
        sleep_ms(3000); // then repeat, forever
    }
}

void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile (
        "movs r0, #4        \n"
        "mov r1, lr         \n"
        "tst r0, r1         \n"
        "beq 1f             \n"
        "mrs r0, psp        \n"
        "b 2f               \n"
        "1: mrs r0, msp     \n"
        "2: ldr r1, =hardfault_blink \n"
        "bx r1              \n"
    );
}

// I_Error() expands to __breakpoint() under NO_IERROR (see i_system.h) --
// a bare BKPT instruction. With no debugger attached that typically
// escalates to HardFault, but depending on DEMCR configuration it can
// route to the DebugMonitor exception instead. Cover both with the same
// handler; the exception stack frame layout is identical either way.
void __attribute__((naked)) isr_debugmonitor(void) {
    __asm volatile (
        "movs r0, #4        \n"
        "mov r1, lr         \n"
        "tst r0, r1         \n"
        "beq 1f             \n"
        "mrs r0, psp        \n"
        "b 2f               \n"
        "1: mrs r0, msp     \n"
        "2: ldr r1, =hardfault_blink \n"
        "bx r1              \n"
    );
}

// Downscales one completed view (SCREENWIDTH x MAIN_VIEWHEIGHT palette-index
// pixels, i.e. frame_buffer[display_frame_index] -- the 3D view only, the
// status bar isn't rendered into it) to WS2812_MATRIX_WIDTH x
// WS2812_MATRIX_HEIGHT by block-averaging in RGB space, and pushes it to the
// LED matrix. `palette` is the same RGB565-ish table (see
// PICO_SCANVIDEO_PIXEL_FROM_RGB8 above) the ILI9341 backend uses per
// scanline; we just decode it back to 8-bit-per-channel here instead.

// Block-averaging many palette colors together washes everything out toward
// mid-gray (contrast loss inherent to downsampling this hard, 8x8 from a
// full 3D view) and, on real hardware, left no pixel ever reading as true
// black -- everything sat at some dim-but-nonzero glow. Two passes, no
// floating point (this project panics on any FP op, see
// pico_set_float_implementation(... none) in CMakeLists.txt):
//
// 1. Black-point shift: crush anything at or below MATRIX_BLACK_SHIFT to
//    true 0, then stretch the remaining range back out to fill 0-255 --
//    "shift blacks to start earlier", so dim-but-not-really-dark input
//    actually reads as off instead of a faint glow.
// 2. The existing gamma-2-ish curve, blended toward the full curve (was
//    50/50 with linear; cranked up further since even darker shadows were
//    still wanted after step 1 alone).
// A separate flat percentage reduction and a uniform 0-255->0-LIMIT scale
// (replacing ws2812_matrix_set_pixel()'s own per-pixel peak-normalization)
// were both tried and reverted -- with MATRIX_BRIGHTNESS_LIMIT this small,
// either one collapsed nearly the whole frame to 0 before it could round up
// to a visible level. Brightness tuning for now happens only via
// MATRIX_BRIGHTNESS_LIMIT itself (see CMakeLists.txt) and set_pixel's own
// per-pixel normalization, which is what actually produced a visible image.
#define MATRIX_BLACK_SHIFT 56
static inline uint8_t matrix_contrast(uint8_t v) {
    uint8_t shifted = (v <= MATRIX_BLACK_SHIFT) ? 0 :
            (uint8_t) (((uint16_t) (v - MATRIX_BLACK_SHIFT) * 255) / (255 - MATRIX_BLACK_SHIFT));
    uint8_t full_curve = (uint8_t) (((uint16_t) shifted * shifted) / 255);
    return (uint8_t) (((uint16_t) shifted + 3 * (uint16_t) full_curve) / 4);
}

static void matrix_show_frame(const uint8_t *view) {
    enum { BLOCK_W = SCREENWIDTH / WS2812_MATRIX_WIDTH, BLOCK_H = MAIN_VIEWHEIGHT / WS2812_MATRIX_HEIGHT };
    for (int cy = 0; cy < WS2812_MATRIX_HEIGHT; cy++) {
        for (int cx = 0; cx < WS2812_MATRIX_WIDTH; cx++) {
            uint32_t rsum = 0, gsum = 0, bsum = 0;
            for (int y = 0; y < BLOCK_H; y++) {
                const uint8_t *row = view + (cy * BLOCK_H + y) * SCREENWIDTH + cx * BLOCK_W;
                for (int x = 0; x < BLOCK_W; x++) {
                    uint16_t p = palette[row[x]];
                    uint8_t r5 = (p >> PICO_SCANVIDEO_PIXEL_RSHIFT) & 0x1f;
                    uint8_t g5 = (p >> PICO_SCANVIDEO_PIXEL_GSHIFT) & 0x1f;
                    uint8_t b5 = (p >> PICO_SCANVIDEO_PIXEL_BSHIFT) & 0x1f;
                    rsum += (r5 << 3) | (r5 >> 2); // 5 -> 8 bit
                    gsum += (g5 << 3) | (g5 >> 2);
                    bsum += (b5 << 3) | (b5 >> 2);
                }
            }
            const int n = BLOCK_W * BLOCK_H;
            uint8_t r = matrix_contrast(rsum / n);
            uint8_t g = matrix_contrast(gsum / n);
            uint8_t b = matrix_contrast(bsum / n);
            // REVERTED (confirmed too dark on hardware -- with
            // MATRIX_BRIGHTNESS_LIMIT this small, only near-white input
            // survived the divide at all, "only a couple of red pixels, all
            // else dark"): tried scaling 0-255 down to 0-LIMIT uniformly
            // here instead of letting ws2812_matrix_set_pixel() do its own
            // per-pixel peak-normalization. That approach is more "correct"
            // (preserves relative brightness instead of boosting everything
            // non-black up to the ceiling) but needs much more headroom in
            // MATRIX_BRIGHTNESS_LIMIT than 2 to avoid collapsing most of the
            // frame to 0. Passing the contrast-curve output straight through
            // and letting set_pixel's own normalization brighten it back up
            // is what actually produced a visible image on hardware.
            // Mirrored left-to-right on the physical panel (confirmed on
            // hardware) -- flip the destination column while still reading
            // the source left-to-right.
            ws2812_matrix_set_pixel(WS2812_MATRIX_WIDTH - 1 - cx, cy, r, g, b);
        }
    }
    ws2812_matrix_show();
}
#endif

//dahai
 // void display_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void ili9341_infones_frame_timing_register_init();
void /*__scratch_x("scanlines")*/ fill_scanlines() {
#if 1
//dahai
#if SUPPORT_TEXT
    struct scanvideo_scanline_buffer *buffer = scanvideo_begin_scanline_generation_linked(display_video_type == VIDEO_TYPE_TEXT ? 2 : 1, false);
#else
    struct scanvideo_scanline_buffer *buffer = scanvideo_begin_scanline_generation(false);
#endif
#endif

#if USE_INTERP
    need_save = interp_in_use;
    interp_updated = 0;
#endif

    while (buffer) {
        static int8_t last_frame_number = -1;
        int frame = scanvideo_frame_number(buffer->scanline_id);
        int scanline = scanvideo_scanline_number(buffer->scanline_id);
        if ((int8_t) frame != last_frame_number) {
            last_frame_number = frame;
            new_frame_stuff();
            //dahai
            gpio_xor_mask(1<<LED_PIN);

            if(frame < 5 || frame % 10 == 0) {  
                // poconoco
                // Required to fix the initial screen corruption issue. 
                // The first few frames after reset are not rendered correctly due to uninitialized state in the display controller. 
                // Re-initializing the frame timing registers for the first few frames ensures that the display starts rendering correctly.
                #ifdef ILI9341
                ili9341_infones_frame_timing_register_init();
                #endif
                #ifdef ST7789
                st7789_infones_frame_timing_register_init();
                #endif
            }
        }

        DEBUG_PINS_SET(scanline_copy, 1);
        if (display_video_type != VIDEO_TYPE_TEXT) {
            // we don't have text mode -> normal transition yet, but we may for network game, so leaving this here - we would need to put the buffer pointers back
            assert (buffer->data < text_scanline_buffer_start || buffer->data >= text_scanline_buffer_start + TEXT_SCANLINE_BUFFER_TOTAL_WORDS);
            scanline_funcs[display_video_type](buffer->data+1, scanline);
            if (display_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS) {
                assert(scanline < count_of(vpatchlists->vpatch_starters));
                int prev = 0;
                for (int vp = vpatchlists->vpatch_starters[scanline]; vp;) {
                    int next = vpatchlists->vpatch_next[vp];
                    while (vpatchlists->vpatch_next[prev] && vpatchlists->vpatch_next[prev] < vp) {
                        prev = vpatchlists->vpatch_next[prev];
                    }
                    assert(prev != vp);
                    assert(vpatchlists->vpatch_next[prev] != vp);
                    vpatchlists->vpatch_next[vp] = vpatchlists->vpatch_next[prev];
                    vpatchlists->vpatch_next[prev] = vp;
                    prev = vp;
                    vp = next;
                }
                vpatchlist_t *overlays = vpatchlists->overlays[display_overlay_index];
                prev = 0;
                for (int vp = vpatchlists->vpatch_next[prev]; vp; vp = vpatchlists->vpatch_next[prev]) {
                    patch_t *patch = resolve_vpatch_handle(overlays[vp].entry.patch_handle);
                    int yoff = scanline - overlays[vp].entry.y;
                    if (yoff < vpatch_height(patch)) {
                        vpatchlists->vpatch_doff[vp] = draw_vpatch((uint16_t*)(buffer->data + 1), patch, &overlays[vp],
                                                                   vpatchlists->vpatch_doff[vp]);
                        prev = vp;
                    } else {
                        vpatchlists->vpatch_next[prev] = vpatchlists->vpatch_next[vp];
                    }
                }
            }
            uint16_t *p = (uint16_t *) buffer->data;
            p[0] = video_doom_offset_raw_run;
            p[1] = p[2];
            p[2] = SCREENWIDTH - 3;
            buffer->data[SCREENWIDTH / 2 + 1] = video_doom_offset_raw_1p;
            buffer->data[SCREENWIDTH / 2 + 2] = video_doom_offset_end_of_scanline_skip_ALIGN;
            buffer->data_used = SCREENWIDTH / 2 + 3;
            DEBUG_PINS_CLR(scanline_copy, 1);
        } else {
#if SUPPORT_TEXT
            render_text_mode_scanline(buffer, scanline);
#else
            memset(buffer->data + 1, 0, SCREENWIDTH * 2);
            p[0] = video_doom_offset_raw_run;
            p[1] = p[2];
            p[2] = SCREENWIDTH - 3;
            buffer->data[SCREENWIDTH / 2 + 1] = video_doom_offset_raw_1p;
            buffer->data[SCREENWIDTH / 2 + 2] = video_doom_offset_end_of_scanline_skip_ALIGN;
            buffer->data_used = SCREENWIDTH / 2 + 3;
#endif
        }
        //dahai
        scanvideo_end_scanline_generation(buffer);

/*
 *dahai
 */
#ifdef ILI9341

        // dma_channel_wait_for_finish_blocking(display_dma_channel);

        // uint16_t scanline_buffer[SCREENWIDTH];
        // for(int i=0;i<SCREENWIDTH;i++){
        //      scanline_buffer[i] = buffer->data[i];
        //     // spi_write_blocking(DISPLAY_SPI_PORT, &(buffer->data[i]), 2);
        // }
        // // spi_write_blocking(DISPLAY_SPI_PORT, scanline_buffer, 2*SCREENWIDTH);
        
        spi_write_blocking(DISPLAY_SPI_PORT, (uint8_t *)&(buffer->data[2])+1, 2*SCREENWIDTH);

        // dma_channel_set_trans_count(display_dma_channel, SCREENWIDTH*sizeof(uint16_t), false);
        // dma_channel_set_read_addr(display_dma_channel, /*(uint8_t *)*/scanline_buffer, true);   
#endif
#ifdef ST7789
        for(int i=0,j=2; j<SCREENWIDTH*2; i+=2,j+=4){
           memcpy((uint8_t *)&(buffer->data[2])+i+0 , (uint8_t *)&(buffer->data[2])+j+0, sizeof(uint8_t));
           memcpy((uint8_t *)&(buffer->data[2])+i+1 , (uint8_t *)&(buffer->data[2])+j+1, sizeof(uint8_t));
        }
        if(scanline%2 == 0){
          spi_write_blocking(DISPLAY_SPI_PORT, (uint8_t *)&(buffer->data[2])+1, 1*SCREENWIDTH);
        }
#endif
//dahai
        // *((io_rw_32 *) (PPB_BASE + M0PLUS_NVIC_ISPR_OFFSET)) = 1u << 31;

#if 1
//dahai
#if SUPPORT_TEXT
        buffer = scanvideo_begin_scanline_generation_linked(display_video_type == VIDEO_TYPE_TEXT ? 2 : 1, false);
#else
        buffer = scanvideo_begin_scanline_generation(false);
#endif
#endif

    }
#if USE_INTERP
    if (interp_updated && need_save) {
        interp_restore_static(interp0, &interp0_save);
        interp_restore_static(interp1, &interp1_save);
    }
#endif
}
#pragma GCC pop_options

#if PICO_ON_DEVICE
#define LOW_PRIO_IRQ 31
#include "hardware/irq.h"

#if PICO_RP2040
#define NVIC_ISPR_OFFSET M0PLUS_NVIC_ISPR_OFFSET
#else
// LOW_PRIO_IRQ is < 32, so it still lands in the first (of several, on Cortex-M33) ISPR word
#define NVIC_ISPR_OFFSET M33_NVIC_ISPR0_OFFSET
#endif

static void __not_in_flash_func(free_buffer_callback)() {
//    irq_set_pending(LOW_PRIO_IRQ);
    // ^ is in flash by default
    *((io_rw_32 *) (PPB_BASE + NVIC_ISPR_OFFSET)) = 1u << LOW_PRIO_IRQ;
}
#endif

//static semaphore_t init_sem;
static void core1() {
#if RP2350_MATRIX
    // No scanvideo/PIO-VGA/SPI machinery needed at all here: that whole
    // apparatus exists only to feed the ILI9341 over SPI on the beam-timed
    // schedule scanvideo provides. The matrix just needs the same
    // frame-ready handoff new_frame_stuff() already does for that backend,
    // polled directly instead of from a scanline IRQ.
    sem_release(&core1_launch);
    uint32_t last_shown_frame_serial = ~0u; // matrix_frame_serial starts at 0, so the first frame always shows
    // ws2812_matrix_show() calls need a minimum spacing -- back-to-back
    // calls with no gap were confirmed (empirically) to hang the PIO SM
    // solid, likely WS2812's own inter-frame latch/reset requirement
    // biting harder than usual in this exact setup. 100ms was confirmed
    // to work reliably; an 8x8 ambient display doesn't need anywhere near
    // DOOM's own frame rate anyway, so this costs nothing visually.
    absolute_time_t next_matrix_update = get_absolute_time();
    while (true) {
        // Serviced first: pd_core1_loop() blocks on a semaphore core0 only
        // releases once real rendering has begun, so a checkpoint request
        // made during early D_DoomMain startup would never be reached if
        // this ran after it.
        matrix_service_checkpoint_request();
        // With core0 now confirmed to reach checkpoint 9 (about to call
        // pd_end_frame()) and block there waiting on core1_done -- exactly
        // as expected while this loop was skipping pd_core1_loop() -- it's
        // time to find out whether the real handshake actually completes.
        pd_core1_loop();
        new_frame_stuff();
        // matrix_frame_serial (not display_frame_index) so single-buffered
        // redraws-in-place (title screen etc, anything outside GS_LEVEL)
        // still trigger an update -- see matrix_frame_serial's declaration.
        if (matrix_frame_serial != last_shown_frame_serial && time_reached(next_matrix_update)) {
            last_shown_frame_serial = matrix_frame_serial;
            matrix_show_frame(frame_buffer[display_frame_index]);
            next_matrix_update = make_timeout_time_ms(100);
        }
        // Neither a flat 20ms nor 150ms unconditional delay HERE (end of the
        // outer loop, i.e. only between pd_core1_loop() calls as a whole)
        // prevented the freeze -- confirmed on hardware, twice. Only
        // checkpoints, which interpose pacing *between pd_core1_loop()'s
        // own internal handshake stages*, have ever worked. Testing that
        // shape directly now via MATRIX_HANDSHAKE_PACE() in pd_core1_loop()
        // itself (pd_render.cpp) instead of a lump delay out here.
        tight_loop_contents();
    }
#else
#if !PICO_ON_DEVICE
    void simulate_video_pio_video_doom(const uint32_t *dma_data, uint32_t dma_data_size,
                                       uint16_t *pixel_buffer, int32_t max_pixels, int32_t expected_width, bool overlay);
    scanvideo_set_simulate_scanvideo_pio_fn(VIDEO_DOOM_PROGRAM_NAME, simulate_video_pio_video_doom);
#endif
    scanvideo_setup(&VGA_MODE);
//    sem_release(&init_sem);
#if PICO_ON_DEVICE
    irq_set_priority(LOW_PRIO_IRQ, 0xC0); // Lower than lockout priority
    irq_set_exclusive_handler(LOW_PRIO_IRQ, fill_scanlines);
    irq_set_enabled(LOW_PRIO_IRQ, true);
    //dahai
    scanvideo_set_scanline_release_fn(free_buffer_callback);
#endif
    scanvideo_timing_enable(true);
#if PICO_ON_DEVICE
    irq_set_pending(LOW_PRIO_IRQ);
    multicore_lockout_victim_init();
#endif
    sem_release(&core1_launch);
    while (true) {
        pd_core1_loop();
#if PICO_ON_DEVICE
        tight_loop_contents();
#else
        fill_scanlines();
#endif
    }
#endif
}


/*
 *dahai
 */
static void display_write_command(const uint8_t command)
{
    /* Set DC low to denote incoming command. */
    gpio_put(DISPLAY_PIN_DC, 0);

    /* Set CS low to reserve the SPI bus. */
    gpio_put(DISPLAY_PIN_CS, 0);

    spi_write_blocking(DISPLAY_SPI_PORT, &command, 1);

    /* Set CS high to ignore any traffic on SPI bus. */
    gpio_put(DISPLAY_PIN_CS, 1);
}

static void display_write_data(const uint8_t *data, size_t length)
{
    size_t sent = 0;

    if (0 == length) {
        return;
    };

    /* Set DC high to denote incoming data. */
    gpio_put(DISPLAY_PIN_DC, 1);

    /* Set CS low to reserve the SPI bus. */
    gpio_put(DISPLAY_PIN_CS, 0);

    spi_write_blocking(DISPLAY_SPI_PORT, data, length);

    /* Set CS high to ignore any traffic on SPI bus. */
    gpio_put(DISPLAY_PIN_CS, 1);
}
 void display_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    uint8_t command;
    uint8_t data[4];
    static uint16_t prev_x1, prev_x2, prev_y1, prev_y2;

    x1 = x1 + DISPLAY_OFFSET_X;
    y1 = y1 + DISPLAY_OFFSET_Y;
    x2 = x2 + DISPLAY_OFFSET_X;
    y2 = y2 + DISPLAY_OFFSET_Y;

    /* Change column address only if it has changed. */
    if ((prev_x1 != x1 || prev_x2 != x2)) {
        display_write_command(DCS_SET_COLUMN_ADDRESS);
        data[0] = x1 >> 8;
        data[1] = x1 & 0xff;
        data[2] = x2 >> 8;
        data[3] = x2 & 0xff;
        display_write_data(data, 4);

        prev_x1 = x1;
        prev_x2 = x2;
    }

    /* Change page address only if it has changed. */
    if ((prev_y1 != y1 || prev_y2 != y2)) {
        display_write_command(DCS_SET_PAGE_ADDRESS);
        data[0] = y1 >> 8;
        data[1] = y1 & 0xff;
        data[2] = y2 >> 8;
        data[3] = y2 & 0xff;
        display_write_data(data, 4);

        prev_y1 = y1;
        prev_y2 = y2;
    }
    // 
    display_write_command(DCS_WRITE_MEMORY_START);
}
static void display_spi_master_init()
{
    // https://github.com/Bodmer/TFT_eSPI/discussions/2432
// Get the processor sys_clk frequency in Hz
 uint32_t freq = clock_get_hz(clk_sys);

 // clk_peri does not have a divider, so input and output frequencies will be the same
 clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    freq,
                    freq);


    gpio_set_function(DISPLAY_PIN_DC, GPIO_FUNC_SIO);
    gpio_set_dir(DISPLAY_PIN_DC, GPIO_OUT);

    gpio_set_function(DISPLAY_PIN_CS, GPIO_FUNC_SIO);
    gpio_set_dir(DISPLAY_PIN_CS, GPIO_OUT);

    gpio_set_function(DISPLAY_PIN_CLK,  GPIO_FUNC_SPI);
    gpio_set_function(DISPLAY_PIN_MOSI, GPIO_FUNC_SPI);

    if (DISPLAY_PIN_MISO > 0) {
        gpio_set_function(DISPLAY_PIN_MISO, GPIO_FUNC_SPI);
    }

    /* Set CS high to ignore any traffic on SPI bus. */
    gpio_put(DISPLAY_PIN_CS, 1);

    spi_init(DISPLAY_SPI_PORT, DISPLAY_SPI_CLOCK_SPEED_HZ);

    uint32_t baud = spi_set_baudrate(DISPLAY_SPI_PORT, DISPLAY_SPI_CLOCK_SPEED_HZ);
    uint32_t peri = clock_get_hz(clk_peri);
    uint32_t sys = clock_get_hz(clk_sys);

// DMA init
    // display_dma_channel = dma_claim_unused_channel(true);
    // dma_channel_config channel_config = dma_channel_get_default_config(display_dma_channel);
    // channel_config_set_transfer_data_size(&channel_config, DMA_SIZE_8);
    // if (spi0 == DISPLAY_SPI_PORT) {
    //     channel_config_set_dreq(&channel_config, DREQ_SPI0_TX);
    // } else {
    //     channel_config_set_dreq(&channel_config, DREQ_SPI1_TX);
    // }
    // dma_channel_set_config(display_dma_channel, &channel_config, false);
    // dma_channel_set_write_addr(display_dma_channel, &spi_get_hw(DISPLAY_SPI_PORT)->dr, false);

}

/*
 *
 */
void display_init()
{

    /* Init the spi driver. */
    display_spi_master_init();
    sleep_ms(100);

    /* Reset the display. */
    if (DISPLAY_PIN_RST > 0) {
        gpio_set_function(DISPLAY_PIN_RST, GPIO_FUNC_SIO);
        gpio_set_dir(DISPLAY_PIN_RST, GPIO_OUT);

        gpio_put(DISPLAY_PIN_RST, 0);
        sleep_ms(100);
        gpio_put(DISPLAY_PIN_RST, 1);
        sleep_ms(100);
    }

    /* Send minimal init commands. */
    display_write_command(DCS_SOFT_RESET);
    sleep_ms(200);

    display_write_command(DCS_SET_ADDRESS_MODE);
    uint8_t mode1 = DISPLAY_ADDRESS_MODE;
    mode1 ^= 0x48; // 0x08 bit flips the red and blue colors, 0.40 bit flips the image horizontally, adjust as needed
    display_write_data(&mode1, 1);

    display_write_command(DCS_SET_PIXEL_FORMAT);
    uint8_t mode2 = DISPLAY_PIXEL_FORMAT;
    display_write_data(&mode2, 1);

    display_write_command(DCS_WRITE_DISPLAY_BRIGHTNESS);
    uint8_t brightness = 0xff; // Increased hardware brightness from 0xaa to 0xff (Max)
    display_write_data(&brightness, 1);

    // display_write_command(DCS_GAMMA_SET);
    // uint8_t gamma = 0x2;
    // display_write_data(&gamma, 1);

#ifdef DISPLAY_INVERT
    display_write_command(DCS_ENTER_INVERT_MODE);

#else
    display_write_command(DCS_EXIT_INVERT_MODE);
#endif

    display_write_command(DCS_EXIT_SLEEP_MODE);
    sleep_ms(200);

    display_write_command(DCS_SET_DISPLAY_ON);
    sleep_ms(200);
// // ENDIAN
//     display_write_command(0xf6);
//     display_write_data(0x0001,2);
//     display_write_data(0x0000,2);
//     display_write_data(0x0020,2); // 0x0020 = LSB first

    /* Enable backlight */
    if (DISPLAY_PIN_BL > 0) {
        gpio_set_function(DISPLAY_PIN_BL, GPIO_FUNC_SIO);
        gpio_set_dir(DISPLAY_PIN_BL, GPIO_OUT);

        gpio_put(DISPLAY_PIN_BL, 1);
    }

    /* Set the default viewport to full screen. */
    display_set_address(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);


}
void display_clear()
{
#ifdef ILI9341
    display_set_address(0,0,320-1,240-1);
    BYTE pixel[2]={0x00,0x00};
    for(int i=0;i<320*240;i+=1){
        display_write_data(pixel,2);
    }
#endif
#ifdef ST7789
    display_set_address(0,0,160-1,128-1);
    BYTE pixel[2]={0x00,0x00};
    for(int i=0;i<160*128;i+=1){
        display_write_data(pixel,2);
    }
#endif
  

}
/*
 *  setting column and page, start and stop
 */
void ili9341_infones_frame_timing_register_init()
{
        uint8_t command;
        uint8_t data[4];
        int x=0;



#if 0
        display_set_address(x+((320-256)/2), 4, (x+((320-256)/2)+FRAME_COLUMN_WIDTH-1), (240-4-1));
#endif
        display_set_address(0, 20, SCREENWIDTH-1, SCREENHEIGHT-1+20);

////
        /*
         *   keep chip select active, let the next data be written continuously
         */
        gpio_put(DISPLAY_PIN_DC, 1);
        gpio_put(DISPLAY_PIN_CS, 0);

}
void st7789_infones_frame_timing_register_init()
{
        uint8_t command;
        uint8_t data[4];
        int x=0;



        // display_set_address(0, 0, (SCREENWIDTH)/2, (MAIN_VIEWHEIGHT)/2);
        display_set_address(0, 10, (SCREENWIDTH)/2, 96+3+10);

////
        /*
         *   keep chip select active, let the next data be written continuously
         */
        gpio_put(DISPLAY_PIN_DC, 1);
        gpio_put(DISPLAY_PIN_CS, 0);

}

void I_InitGraphics(void)
{
    //dahai
#if !RP2350_MATRIX
    // On boards that reuse PICO_DEFAULT_LED_PIN as the matrix's data-in
    // (e.g. GPIO25 on the RP2350 matrix board), claiming it as a plain SIO
    // output here would race the matrix driver's own gpio_set_function()
    // for the same pin. There's no separate status LED to drive in matrix
    // mode anyway, so skip this entirely rather than depend on ordering.
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
#endif

#if RP2350_MATRIX
    ws2812_matrix_init(MATRIX_DATA_PIN);
    // Direct calls are safe here (and only here): core1 doesn't exist yet,
    // so there's no other caller of the matrix driver to race against.
    // Once multicore_launch_core1() below runs, all further checkpoints
    // must go through matrix_request_checkpoint() instead. The
    // white/yellow/cyan startup blinks that used to live here and below
    // were removed -- confirmed passing reliably, every single boot, for
    // a long stretch of testing; no longer earning their keep (and each
    // held the whole matrix lit for 800ms, adding to sustained LED heat).
#else
    display_init();
    display_clear();
#ifdef ILI9341
    #pragma message "ILI9341 defined."
    ili9341_infones_frame_timing_register_init();
#endif
#ifdef ST7789
    #pragma message "ST7789 defined"
    st7789_infones_frame_timing_register_init();
#endif
#endif


    sem_init(&render_frame_ready, 0, 2);
    sem_init(&display_frame_freed, 1, 2);
    sem_init(&core1_launch, 0, 1);
    pd_init();
    multicore_launch_core1(core1);
    // wait for core1 launch as it may do malloc and we have no mutex around that
    sem_acquire_blocking(&core1_launch);
#if RP2350_MATRIX
    // core1 is running now -- must go through the request mechanism.
    matrix_request_checkpoint(2); // core1 launched, I_InitGraphics about to return
#endif
#if USE_ZONE_FOR_MALLOC
    disallow_core1_malloc = true;
#endif
    initialized = true;
}

// Bind all variables controlling video options into the configuration
// file system.
void I_BindVideoVariables(void)
{
//    M_BindIntVariable("use_mouse",                 &usemouse);
//    M_BindIntVariable("fullscreen",                &fullscreen);
//    M_BindIntVariable("video_display",             &video_display);
//    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
//    M_BindIntVariable("integer_scaling",           &integer_scaling);
//    M_BindIntVariable("vga_porch_flash",           &vga_porch_flash);
//    M_BindIntVariable("startup_delay",             &startup_delay);
//    M_BindIntVariable("fullscreen_width",          &fullscreen_width);
//    M_BindIntVariable("fullscreen_height",         &fullscreen_height);
//    M_BindIntVariable("force_software_renderer",   &force_software_renderer);
//    M_BindIntVariable("max_scaling_buffer_pixels", &max_scaling_buffer_pixels);
//    M_BindIntVariable("window_width",              &window_width);
//    M_BindIntVariable("window_height",             &window_height);
//    M_BindIntVariable("grabmouse",                 &grabmouse);
//    M_BindStringVariable("video_driver",           &video_driver);
//    M_BindStringVariable("window_position",        &window_position);
//    M_BindIntVariable("usegamma",                  &usegamma);
//    M_BindIntVariable("png_screenshots",           &png_screenshots);
}

//
// I_StartTic
//
void I_StartTic (void)
{
    if (!initialized)
    {
        return;
    }

    I_GetEvent();
//
//    if (usemouse && !nomouse && window_focused)
//    {
//        I_ReadMouse();
//    }
//
//    if (joywait < I_GetTime())
//    {
//        I_UpdateJoystick();
//    }
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

int I_GetPaletteIndex(int r, int g, int b)
{
    return 0;
}

#if !NO_USE_ENDDOOM
void I_Endoom(byte *endoom_data) {
    uint32_t size;
    uint8_t *wa = pd_get_work_area(&size);
    assert(size >=TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 + 80*25*2 + 4096);
    text_screen_cpy = wa;
    text_font_cpy = text_screen_cpy + 80 * 25 * 2;
    text_scanline_buffer_start = (uint32_t *) (text_font_cpy + 4096);
#if 0
    static_assert(sizeof(normal_font_data) == 4096, "");
    memcpy(text_font_cpy, normal_font_data, sizeof(normal_font_data));
    memcpy(text_screen_cpy, endoom_data, 80 * 25 * 2);
#else
    static_assert(TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 > 1024 + 512, "");
    uint8_t *tmp_buf = (uint8_t *)text_scanline_buffer_start;
    uint16_t *decoder = (uint16_t *)(tmp_buf + 512);
    th_bit_input bi;
    th_bit_input_init(&bi, normal_font_data_z);
    decode_data(text_font_cpy, 4096, &bi, decoder, 512, tmp_buf, 512);
    th_bit_input_init(&bi, endoom_data);
    // text
    decode_data(text_screen_cpy, 80*25, &bi, decoder, 512, tmp_buf, 512);
    // attr
    decode_data(text_screen_cpy+80*25, 80*25, &bi, decoder, 512, tmp_buf, 512);
    static_assert(TEXT_SCANLINE_BUFFER_TOTAL_WORDS * 4 > 80*25*2, "");
    // re-interlace the text & attr
    memcpy(tmp_buf, text_screen_cpy, 80*25*2);
    for(int i=0;i<80*25;i++) {
        text_screen_cpy[i*2] = tmp_buf[i];
        text_screen_cpy[i*2+1] = tmp_buf[80*25 + i];
    }
#endif
    text_screen_data = text_screen_cpy;
}
#endif

void I_GraphicsCheckCommandLine(void)
{
//    int i;
//
//    //!
//    // @category video
//    // @vanilla
//    //
//    // Disable blitting the screen.
//    //
//
//    noblit = M_CheckParm ("-noblit");
//
//    //!
//    // @category video
//    //
//    // Don't grab the mouse when running in windowed mode.
//    //
//
//    nograbmouse_override = M_ParmExists("-nograbmouse");
//
//    // default to fullscreen mode, allow override with command line
//    // nofullscreen because we love prboom
//
//    //!
//    // @category video
//    //
//    // Run in a window.
//    //
//
//    if (M_CheckParm("-window") || M_CheckParm("-nofullscreen"))
//    {
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    //
//    // Run in fullscreen mode.
//    //
//
//    if (M_CheckParm("-fullscreen"))
//    {
//        fullscreen = true;
//    }
//
//    //!
//    // @category video
//    //
//    // Disable the mouse.
//    //
//
//    nomouse = M_CheckParm("-nomouse") > 0;
//
//    //!
//    // @category video
//    // @arg <x>
//    //
//    // Specify the screen width, in pixels. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-width", 1);
//
//    if (i > 0)
//    {
//        window_width = atoi(myargv[i + 1]);
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    // @arg <y>
//    //
//    // Specify the screen height, in pixels. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-height", 1);
//
//    if (i > 0)
//    {
//        window_height = atoi(myargv[i + 1]);
//        fullscreen = false;
//    }
//
//    //!
//    // @category video
//    // @arg <WxY>
//    //
//    // Specify the dimensions of the window. Implies -window.
//    //
//
//    i = M_CheckParmWithArgs("-geometry", 1);
//
//    if (i > 0)
//    {
//        int w, h, s;
//
//        s = sscanf(myargv[i + 1], "%ix%i", &w, &h);
//        if (s == 2)
//        {
//            window_width = w;
//            window_height = h;
//            fullscreen = false;
//        }
//    }
//
//    //!
//    // @category video
//    //
//    // Don't scale up the screen. Implies -window.
//    //
//
//    if (M_CheckParm("-1"))
//    {
//        SetScaleFactor(1);
//    }
//
//    //!
//    // @category video
//    //
//    // Double up the screen to 2x its normal size. Implies -window.
//    //
//
//    if (M_CheckParm("-2"))
//    {
//        SetScaleFactor(2);
//    }
//
//    //!
//    // @category video
//    //
//    // Double up the screen to 3x its normal size. Implies -window.
//    //
//
//    if (M_CheckParm("-3"))
//    {
//        SetScaleFactor(3);
//    }
}

// Check if we have been invoked as a screensaver by xscreensaver.

void I_CheckIsScreensaver(void)
{
}

void I_DisplayFPSDots(boolean dots_on)
{
}

#if PICO_ON_DEVICE
bool video_doom_adapt_for_mode(const struct scanvideo_pio_program *program, const struct scanvideo_mode *mode,
                               struct scanvideo_scanline_buffer *missing_scanvideo_scanline_buffer, uint16_t *modifiable_instructions) {
    missing_scanvideo_scanline_buffer->data = missing_scanline_data;
    missing_scanvideo_scanline_buffer->data_used = missing_scanvideo_scanline_buffer->data_max = sizeof(missing_scanline_data) / 4;
    return true;
}

pio_sm_config video_doom_configure_pio(pio_hw_t *pio, uint sm, uint offset) {
    pio_sm_config config = video_24mhz_composable_default_program_get_default_config(offset);
    scanvideo_default_configure_pio(pio, sm, offset, &config, false);
    return config;
}
#else
void simulate_video_pio_video_doom(const uint32_t *dma_data, uint32_t dma_data_size,
                                   uint16_t *pixel_buffer, int32_t max_pixels, int32_t expected_width, bool overlay) {
    const uint16_t *it = (uint16_t *) dma_data;
    assert(!(3u & (uintptr_t) dma_data));
    const uint16_t *const __unused dma_data_end = (uint16_t *) (dma_data + dma_data_size);
    const uint16_t *const pixels_end = (uint16_t *) (pixel_buffer + max_pixels);
    uint16_t *pixels = pixel_buffer;
    bool __unused ok = false;
    bool done = false;
    bool __unused last_was_black = true; // in case no pixels
    const uint16_t display_enable_bit = PICO_SCANVIDEO_ALPHA_MASK; // for now
    do {
        uint16_t cmd = *it++;
        switch (cmd) {
            case video_doom_offset_nop_raw:
                break;
            case video_doom_offset_end_of_scanline_skip_ALIGN:
                it++;
                // fall thru
            case video_doom_offset_end_of_scanline_ALIGN:
                done = ok = true;
                break;
            case video_doom_offset_raw_run_half: {
                assert(pixels < pixels_end);
                uint16_t c = *it++;
                if (!overlay || (c & display_enable_bit))
                    *pixels++ = c;
                else
                    pixels++;
                uint16_t len = *it++;
                for (int i = 0; i < len + 2; i++) {
                    assert(pixels < pixels_end);
                    c = *it++;
                    if (!overlay || (c & display_enable_bit))
                        *pixels++ = c;
                    else
                        pixels++;
                }
                last_was_black = !c;
                break;
            }
            case video_doom_offset_raw_1p_half: {
                uint16_t c;
                if (pixels == pixels_end) {
                    c = *it++;
                    assert(!c); // must end with black
                } else {
                    assert(pixels < pixels_end);
                    c = *it++;
                    if (!overlay || (c & display_enable_bit))
                        *pixels++ = c;
                    else
                        pixels++;
                }
                last_was_black = !c;
                break;
            }
            case video_doom_offset_raw_run: {
                assert(pixels < pixels_end);
                uint16_t c = *it++;
                if (!overlay || (c & display_enable_bit))
                    *pixels++ = c, *pixels++ = c;
                else
                    pixels+=2;
                uint16_t len = *it++;
                for (int i = 0; i < len + 2; i++) {
                    assert(pixels < pixels_end);
                    c = *it++;
                    if (!overlay || (c & display_enable_bit))
                        *pixels++ = c, *pixels++ = c;
                    else
                        pixels+=2;
                }
                last_was_black = !c;
                break;
            }
            case video_doom_offset_raw_1p: {
                uint16_t c;
                if (pixels == pixels_end) {
                    c = *it++;
                    assert(!c); // must end with black
                } else {
                    assert(pixels < pixels_end);
                    c = *it++;
                    if (!overlay || (c & display_enable_bit))
                        *pixels++ = c, *pixels++ = c;
                    else
                        pixels += 2;
                }
                last_was_black = !c;
                break;
            }
            default:
                assert(cmd < 32);
                assert(false);
                done = true;
        }
    } while (!done);
    assert(ok);
    assert(it == dma_data_end);
    assert(!(3u & (uintptr_t) (it))); // should end on dword boundary
#if 0
    // should probably have this back ignored for now because of overlays which don't bother
    if (!overlay) {
        assert(!expected_width || pixels == pixel_buffer +
                                            expected_width); // with the correct number of pixels (one more because we stick a black pixel on the end)
    }
#else
    if (expected_width && pixels < pixel_buffer + expected_width) {
        // black out rest of line
        if (!overlay) memset(pixels, 0, (expected_width - (pixels - pixel_buffer)) * sizeof(uint16_t));
    }
#endif
    assert(last_was_black);
}
#endif

#endif
