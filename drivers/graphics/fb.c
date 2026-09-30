#include <drivers/graphics.h>
#include <drivers/tty_console.h>
#include <core/core.h>
#include <core/core_printk.h>
#include <x86_cpu.h>
#include <x86_mmu.h>
#include <iru_string.h>

/* Linear framebuffer over a Limine graphics mode.
 *
 * Mapping: a private 1 GiB kernel-half window at PML4[507] (just below
 * the direct map) is backed by one static PDPT + one static PD using
 * 2 MiB pages with PCD|PWT (uncacheable). The panel is only ever touched
 * through this window, never through the write-back direct-map alias.
 * Framebuffers larger than the window, unaligned bases, or non-RGB
 * modes are refused and the legacy VGA text path stays in charge. */

#define FB_PML4_INDEX   507u
#define FB_VIRT_BASE    (0xFFFFFD8000000000ull)
#define FB_MAX_2M_PAGES 512u
#define FB_2M           (2u * 1024u * 1024u)

static u64 fb_pdpt[512] __attribute__((aligned(4096)));
static u64 fb_pd[512] __attribute__((aligned(4096)));

static int g_present;
static u64 g_phys, g_w, g_h, g_pitch;
static u32 g_bpp;
static u8 g_rs, g_rn, g_gs, g_gn, g_bs, g_bn;
static u8 *g_fb;

static u32 fb_pack(u8 r, u8 g, u8 b)
{
    u32 v = 0;
    if (g_rn)
        v |= (u32)(((u32)r >> (8 - g_rn)) << g_rs);
    if (g_gn)
        v |= (u32)(((u32)g >> (8 - g_gn)) << g_gs);
    if (g_bn)
        v |= (u32)(((u32)b >> (8 - g_bn)) << g_bs);
    return v;
}

static void fb_write_px(u64 x, u64 y, u32 px)
{
    u8 *p = g_fb + y * g_pitch + x * (g_bpp / 8u);
    if (g_bpp == 32) {
        p[0] = (u8)(px & 0xFFu);
        p[1] = (u8)((px >> 8) & 0xFFu);
        p[2] = (u8)((px >> 16) & 0xFFu);
        p[3] = (u8)((px >> 24) & 0xFFu);
    } else {    /* 24 */
        p[0] = (u8)(px & 0xFFu);
        p[1] = (u8)((px >> 8) & 0xFFu);
        p[2] = (u8)((px >> 16) & 0xFFu);
    }
}

void fb_putpixel(u64 x, u64 y, u32 xrgb)
{
    u8 r, g, b;
    if (!g_present || x >= g_w || y >= g_h)
        return;
    r = (u8)((xrgb >> 16) & 0xFFu);
    g = (u8)((xrgb >> 8) & 0xFFu);
    b = (u8)(xrgb & 0xFFu);
    fb_write_px(x, y, fb_pack(r, g, b));
}

void fb_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 xrgb)
{
    u64 i, j;
    u8 r, g, b;
    u32 px;
    if (!g_present || !w || !h || x >= g_w || y >= g_h)
        return;
    if (x + w > g_w)
        w = g_w - x;
    if (y + h > g_h)
        h = g_h - y;
    r = (u8)((xrgb >> 16) & 0xFFu);
    g = (u8)((xrgb >> 8) & 0xFFu);
    b = (u8)(xrgb & 0xFFu);
    px = fb_pack(r, g, b);
    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            fb_write_px(x + j, y + i, px);
}

void fb_clear(u32 xrgb)
{
    if (!g_present)
        return;
    fb_fill_rect(0, 0, g_w, g_h, xrgb);
}

long fb_blit_rows(u64 x, u64 y, u64 w, u64 h, const u32 *rows)
{
    u64 i, j;
    if (!g_present || !rows || !w || !h || x >= g_w || y >= g_h)
        return -1;
    if (x + w > g_w || y + h > g_h)
        return -1;
    if (w > 2048u || h > 2048u)
        return -1;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            u32 v = rows[j * w + i];
            u8 r = (u8)((v >> 16) & 0xFFu);
            u8 g = (u8)((v >> 8) & 0xFFu);
            u8 b = (u8)(v & 0xFFu);
            fb_write_px(x + i, y + j, fb_pack(r, g, b));
        }
    }
    return (long)(w * h);
}

int fb_present(void)
{
    return g_present;
}

void fb_geometry(u64 *w, u64 *h, u64 *pitch, u32 *bpp)
{
    if (w)
        *w = g_w;
    if (h)
        *h = g_h;
    if (pitch)
        *pitch = g_pitch;
    if (bpp)
        *bpp = g_bpp;
}

/* --- fbcon: 8x8 text console on the panel ------------------------------- */

#include "font8x8_basic.h"

#define FBCON_FG 0xAAAAAAu
#define FBCON_BG 0x000000u

static u64 fbcon_cols, fbcon_rows;
static u64 fbcon_cc, fbcon_cr;
static int fbcon_esc, fbcon_csi, fbcon_priv;
static u64 fbcon_p[8];
static int fbcon_np;
static u32 fbcon_fg = 0xAAAAAAu;
static int fbcon_bold;
static int fbcon_rev;
static int fbcon_last_color = -1;
static char fbcon_cur;

/* Text grid: one char per cell (colors are constant on fbcon), so the
 * mouse arrow can restore what it covers without a hardware back-buffer. */
#define FB_GRID_MAX (160u * 128u)
static char fb_grid_buf[FB_GRID_MAX];
static int fb_grid_ok;

static void fbcon_cell(u64 cc, u64 cr, char ch, u32 fg, u32 bg)
{
    u8 *row;
    u64 r;
    u8 glyph;

    if (cc >= fbcon_cols || cr >= fbcon_rows)
        return;
    if (fb_grid_ok)
        fb_grid_buf[cr * fbcon_cols + cc] = ch;
    glyph = (ch >= 0x20 && ch < 0x7F) ? (u8)ch : (u8)'?';
    row = g_fb + cr * 8u * g_pitch + cc * 8u * (g_bpp / 8u);
    for (r = 0; r < 8; r++) {
        u8 bits = (u8)font8x8_basic[(size_t)glyph][r];
        u64 c;
        for (c = 0; c < 8; c++) {
            u32 px = (bits & (u8)(0x01u << c)) ? fg : bg;
            u8 rr = (u8)((px >> 16) & 0xFFu);
            u8 gg = (u8)((px >> 8) & 0xFFu);
            u8 bb = (u8)(px & 0xFFu);
            u8 *p = row + r * g_pitch + c * (g_bpp / 8u);
            u32 out = fb_pack(rr, gg, bb);
            if (g_bpp == 32) {
                p[0] = (u8)(out & 0xFFu);
                p[1] = (u8)((out >> 8) & 0xFFu);
                p[2] = (u8)((out >> 16) & 0xFFu);
                p[3] = 0;
            } else {
                p[0] = (u8)(out & 0xFFu);
                p[1] = (u8)((out >> 8) & 0xFFu);
                p[2] = (u8)((out >> 16) & 0xFFu);
            }
        }
    }
}

static void fbcon_scroll(void)
{
    u64 rowbytes = g_pitch * 8u;
    u64 body = (fbcon_rows - 1u) * rowbytes;
    u8 *top = g_fb;

    if (fbcon_rows < 2)
        return;
    memcpy(top, top + rowbytes, (size_t)body);
    memset(top + body, 0, (size_t)rowbytes);
    if (fb_grid_ok) {
        memmove(fb_grid_buf, fb_grid_buf + fbcon_cols,
                (size_t)(fbcon_rows - 1) * fbcon_cols);
        memset(fb_grid_buf + (fbcon_rows - 1) * fbcon_cols, ' ',
               (size_t)fbcon_cols);
    }
}

static void fbcon_hide_cursor(void)
{
    fbcon_cell(fbcon_cc, fbcon_cr, ' ', fbcon_fg, FBCON_BG);
}

static void fbcon_newline(void)
{
    fbcon_hide_cursor();
    fbcon_cc = 0;
    if (++fbcon_cr >= fbcon_rows) {
        fbcon_cr = fbcon_rows - 1;
        fbcon_scroll();
    }
}

static void fbcon_show_cursor(void)
{
    fbcon_cell(fbcon_cc, fbcon_cr, fbcon_cur, fbcon_fg, FBCON_BG);
}

static void fbcon_putchar(char c)
{
    if (!g_present)
        return;

    if (fbcon_esc) {
        if (!fbcon_csi) {
            if (c == '[') {
                fbcon_csi = 1;
                fbcon_np = 0;
                fbcon_p[0] = fbcon_p[1] = 0;
            } else {
                fbcon_esc = 0;
            }
            return;
        }
        if (c >= '0' && c <= '9') {
            if (fbcon_np < 2)
                fbcon_p[fbcon_np] = fbcon_p[fbcon_np] * 10u +
                                    (u64)(c - '0');
            return;
        }
        if (c == ';') {
            if (fbcon_np < 1)
                fbcon_np = 1;
            return;
        }
        fbcon_esc = fbcon_csi = 0;
        if (c == 'J') {
            if (fbcon_p[0] == 2) {
                fb_clear(FBCON_BG);
                if (fb_grid_ok)
                    memset(fb_grid_buf, ' ',
                           (size_t)fbcon_cols * fbcon_rows);
                fbcon_cc = fbcon_cr = 0;
            }
        } else if (c == 'H') {
            fbcon_hide_cursor();
            fbcon_cc = fbcon_cr = 0;
        } else if (c == 'C') {
            u64 n = fbcon_p[0] ? fbcon_p[0] : 1;
            fbcon_hide_cursor();
            fbcon_cc += n;
            if (fbcon_cc >= fbcon_cols)
                fbcon_cc = fbcon_cols - 1;
        }
        /* 'm' (colors) and anything else: ignored. */
        return;
    }
    if (c == 0x1B) {
        fbcon_esc = 1;
        return;
    }

    if (c == '\n') {
        fbcon_newline();
    } else if (c == '\r') {
        fbcon_hide_cursor();
        fbcon_cc = 0;
    } else if (c == '\b' || c == 0x7F) {
        if (fbcon_cc > 0) {
            fbcon_hide_cursor();
            fbcon_cc--;
            fbcon_cell(fbcon_cc, fbcon_cr, ' ', fbcon_fg, FBCON_BG);
        }
    } else if (c == '\t') {
        do {
            fbcon_hide_cursor();
            if (++fbcon_cc >= fbcon_cols) {
                fbcon_cc = 0;
                if (++fbcon_cr >= fbcon_rows) {
                    fbcon_cr = fbcon_rows - 1;
                    fbcon_scroll();
                }
            }
        } while (fbcon_cc % 8u);
    } else {
        fbcon_hide_cursor();
        fbcon_cell(fbcon_cc, fbcon_cr, c, fbcon_fg, FBCON_BG);
        fbcon_cur = c;
        if (++fbcon_cc >= fbcon_cols) {
            fbcon_cc = 0;
            if (++fbcon_cr >= fbcon_rows) {
                fbcon_cr = fbcon_rows - 1;
                fbcon_scroll();
            }
        }
    }
    fbcon_show_cursor();
}

static void fbcon_write(struct console_device *dev, char c)
{
    (void)dev;
    fbcon_putchar(c);
}

static struct console_device g_fbcon = {
    .name = "fbcon",
    .write = fbcon_write,
};

/* --- mouse arrow overlay ------------------------------------------------
 * The arrow is drawn directly into the panel (it is not part of the
 * grid). Moving it restores the grid cells underneath the old rectangle,
 * so only what fbcon itself printed survives; userland blits (desktop)
 * repaint at their own cadence and simply cover the arrow. */

#define FBM_W 12
#define FBM_H 16
static const char *fbm_arrow[FBM_H] = {
    "B...........",
    "BB..........",
    "BWB.........",
    "BWWB........",
    "BWWWB.......",
    "BWWWWB......",
    "BWWWWWB.....",
    "BWWWWWWB....",
    "BWWWBBBB....",
    "BBWB........",
    "BWB.........",
    "B..B........",
    "...BB.......",
    ".....B.B....",
    ".....BBB....",
    "......BB....",
};
static int fbm_x = -1, fbm_y = -1;

static void fb_rect_restore(int x, int y, int w, int h)
{
    u64 c0 = (u64)x / 8u, c1 = (u64)(x + w - 1) / 8u;
    u64 r0 = (u64)y / 8u, r1 = (u64)(y + h - 1) / 8u;
    u64 cc, cr;

    for (cr = r0; cr <= r1 && cr < fbcon_rows; cr++)
        for (cc = c0; cc <= c1 && cc < fbcon_cols; cc++)
            fbcon_cell(cc, cr, fb_grid_buf[cr * fbcon_cols + cc],
                       fbcon_fg, FBCON_BG);
}

void fb_mouse_place(int x, int y, int buttons)
{
    int r, c;

    (void)buttons;
    if (!g_present || !fb_grid_ok)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if ((u64)x > g_w - 1)
        x = (int)g_w - 1;
    if ((u64)y > g_h - 1)
        y = (int)g_h - 1;

    if (fbm_x >= 0)
        fb_rect_restore(fbm_x, fbm_y, FBM_W, FBM_H);
    fbm_x = x;
    fbm_y = y;
    for (r = 0; r < FBM_H; r++) {
        for (c = 0; c < FBM_W; c++) {
            char px = fbm_arrow[r][c];
            if (px == '.')
                continue;
            fb_putpixel((u64)(x + c), (u64)(y + r),
                        px == 'W' ? 0xFFFFFFu : 0x000000u);
        }
    }
}

void fb_init_early(u64 phys, u64 width, u64 height, u64 pitch, u32 bpp,
                   u8 model, u8 rs, u8 rn, u8 gs, u8 gn, u8 bs, u8 bn)
{
    u64 bytes, pages, i;
    u64 base;

    if (!phys || !width || !height || !pitch)
        return;
    if (model != 1 || (bpp != 32 && bpp != 24))
        return;
    if (phys & (FB_2M - 1u))
        return;   /* 2 MiB pages need an aligned base; else stay on text */

    bytes = pitch * height;
    pages = (bytes + FB_2M - 1u) / FB_2M;
    if (!pages || pages > FB_MAX_2M_PAGES)
        return;

    for (i = 0; i < 512; i++) {
        fb_pdpt[i] = 0;
        fb_pd[i] = 0;
    }
    for (i = 0; i < pages; i++) {
        fb_pd[i] = (phys + i * FB_2M) | X64_PAGE_PRESENT | X64_PAGE_WRITE |
                   X64_PAGE_PCD | X64_PAGE_PWT | X64_PAGE_HUGE;
    }
    fb_pdpt[0] = virt_to_phys((uintptr_t)fb_pd) | X64_PAGE_PRESENT |
                 X64_PAGE_WRITE;
    /* Reach the PML4 through the direct map, not through its link-time
     * address: the early tables sit at a physical-low address, so a
     * high-half RIP-relative displacement cannot reach them (truncated
     * R_X86_64_PC32 under -mcmodel=kernel). Same idiom as x86_paging.c. */
    u64 *pml4 = (u64 *)phys_to_virt(virt_to_phys((uintptr_t)x64_pml4));
    pml4[FB_PML4_INDEX] = virt_to_phys((uintptr_t)fb_pdpt) |
                          X64_PAGE_PRESENT | X64_PAGE_WRITE;
    /* The identity window is gone by now: reach the framebuffer through the
     * 4 GiB direct map, not its raw physical address. */
    base = FB_VIRT_BASE;
    for (i = 0; i < pages; i++)
        invlpg(base + i * FB_2M);
    cpu_mfence();

    g_phys = phys;
    g_w = width;
    g_h = height;
    g_pitch = pitch;
    g_bpp = bpp;
    g_rs = rs;
    g_rn = rn;
    g_gs = gs;
    g_gn = gn;
    g_bs = bs;
    g_bn = bn;
    g_fb = (u8 *)(uintptr_t)base;
    g_present = 1;

    fbcon_cols = width / 8u;
    fbcon_rows = height / 8u;
    if (!fbcon_cols)
        fbcon_cols = 1;
    if (!fbcon_rows)
        fbcon_rows = 1;
    fbcon_cc = fbcon_cr = 0;
    fbcon_esc = fbcon_csi = fbcon_priv = 0;
    fbcon_fg = FBCON_FG;
    fbcon_bold = 0;
    fbcon_rev = 0;
    fbcon_last_color = -1;
    fb_grid_ok = (fbcon_cols * fbcon_rows <= FB_GRID_MAX) ? 1 : 0;
    if (fb_grid_ok)
        memset(fb_grid_buf, ' ', (size_t)fbcon_cols * fbcon_rows);
    fbm_x = fbm_y = -1;

    fb_clear(FBCON_BG);
    console_register(&g_fbcon);
    pr_info("fb: %llux%llu pitch %llu bpp %u, UC window %p\n",
            width, height, pitch, (unsigned)bpp, (void *)base);
}