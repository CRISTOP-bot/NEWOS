#ifndef DRIVERS_FB_H
#define DRIVERS_FB_H

#include <core/core_types.h>

/* Linear framebuffer driver (Limine-provided graphics mode).
 *
 * The bootloader reports the panel (address/width/height/pitch/bpp/masks);
 * this driver maps it uncacheable, offers raw pixel primitives plus a
 * text console backend (fbcon, 8x8 font) so the shell stays usable when
 * the VGA text buffer is gone, and backs the FBINFO/FBWRITE syscalls.
 * Only 24/32-bit RGB modes are handled; anything else leaves the legacy
 * VGA text path in charge. Inactive on non-Limine boots. */

struct newos_fb_info {
    u64 present;
    u64 width;
    u64 height;
    u64 pitch;
    u32 bpp;
};

/* Early init from bootloader values (physical address). Maps the panel,
 * clears it and registers fbcon. Safe to call with present == 0. */
void fb_init_early(u64 phys, u64 width, u64 height, u64 pitch, u32 bpp,
                   u8 model, u8 rs, u8 rn, u8 gs, u8 gn, u8 bs, u8 bn);

int fb_present(void);
void fb_geometry(u64 *w, u64 *h, u64 *pitch, u32 *bpp);

/* 32-bit XRGB8888 pixel helpers (userland ABI color format). */
void fb_putpixel(u64 x, u64 y, u32 xrgb);
void fb_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 xrgb);
void fb_clear(u32 xrgb);

/* Blit XRGB8888 rows from a kernel buffer into a clipped rectangle.
 * Returns pixels written or -1 on bad geometry. */
long fb_blit_rows(u64 x, u64 y, u64 w, u64 h, const u32 *rows);

/* Draw the mouse arrow at pixel (x, y), restoring the text grid cells
 * under its previous rectangle. Only meaningful when fb_present(). */
void fb_mouse_place(int x, int y, int buttons);

#endif
