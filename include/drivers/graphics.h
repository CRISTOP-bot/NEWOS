// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef DRIVERS_GRAPHICS_H
#define DRIVERS_GRAPHICS_H

#include <core/core_types.h>
#include <lib/kernel/iru_list.h>

struct limine_framebuffer;

/* Dirty rectangle structure for partial screen updates */
typedef struct {
    int x, y, w, h;
    bool active;
} DirtyRect;

/* Framebuffer info structure (for userspace and VFS) */
typedef struct {
    void *address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint16_t bpp;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
} framebuffer_info_t;

/* Color mode for framebuffer conversion */
#define COLOR_MODE_STANDARD 0   // 32-bit RGBA
#define COLOR_MODE_GRAYSCALE 1  // 8-bit grayscale
#define COLOR_MODE_MONOCHROME 2 // 1-bit monochrome (dithered)

/* Initialize graphics subsystem with Limine framebuffer */
void graphics_init(struct limine_framebuffer *fb);

/* Update resolution and color mode */
void graphics_update_resolution(int width, int height, int bpp, void* fb_addr, int color_mode);

/* Get screen dimensions */
int get_screen_width(void);
int get_screen_height(void);

/* Get framebuffer address */
uint64_t graphics_get_fb_addr(void);
int graphics_get_fb_bpp(void);
uint64_t graphics_get_fb_pitch(void);

/* Get framebuffer info for userspace */
void graphics_get_fb_params(framebuffer_info_t *info);

/* Put a single pixel */
void put_pixel(int x, int y, uint32_t color);

/* Get pixel color */
uint32_t graphics_get_pixel(int x, int y);

/* Draw rectangle */
void draw_rect(int x, int y, int w, int h, uint32_t color);

/* Draw character bitmap (8x8) */
void draw_char_bitmap(int x, int y, char c, uint32_t color);

/* Draw string */
void draw_string(int x, int y, const char *s, uint32_t color);

/* Clear back buffer */
void graphics_clear_back_buffer(uint32_t color);

/* Flip buffer (present dirty rects) */
void graphics_flip_buffer(void);

/* Scroll back buffer */
void graphics_scroll_back_buffer(int lines);

/* Mark dirty rectangle */
void graphics_mark_dirty(int x, int y, int w, int h);

/* Mark entire screen dirty */
void graphics_mark_screen_dirty(void);

/* Get dirty rectangle */
DirtyRect graphics_get_dirty_rect(void);

/* Clear dirty rectangle */
void graphics_clear_dirty(void);
void graphics_clear_dirty_no_lock(void);

/* Copy screen buffer */
void graphics_copy_screenbuffer(uint32_t *dest);

/* Copy region */
void graphics_copy_region(uint32_t *src, int y_start, int h);

#endif