#include <drivers/vga_console.h>
#include <core/core.h>
#include <x86_io.h>

/* VGA 80x25 monochrome-on-color text console.
 *
 * The classic MDA frame buffer at 0xB8000 (2 bytes/cell: ASCII + attribute)
 * driven straight through the CRTC cursor registers. Every byte written here
 * is also echoed to serial by the console multiplexer, so a captured serial
 * log stays authoritative for CI. */

#define VGA_TEXT_PHYS      0xB8000ull
#define VGA_COLS           80
#define VGA_ROWS           25
#define VGA_ATTR           0x07    /* light gray on black */

#define VGA_CRTC_INDEX     0x3D4
#define VGA_CRTC_DATA      0x3D5
#define VGA_CURSOR_HI      0x0E
#define VGA_CURSOR_LO      0x0F

static volatile u16 *g_vga_mem;
static size_t g_vga_col = 0;
static size_t g_vga_row = 0;
static int g_vga_esc = 0;        /* skipping a CSI escape sequence */

static void vga_move_cursor(void)
{
    size_t pos = g_vga_row * VGA_COLS + g_vga_col;
    outb(VGA_CRTC_INDEX, VGA_CURSOR_HI);
    outb(VGA_CRTC_DATA, (u8)((pos >> 8) & 0xFF));
    outb(VGA_CRTC_INDEX, VGA_CURSOR_LO);
    outb(VGA_CRTC_DATA, (u8)(pos & 0xFF));
}

static void vga_clear_row(size_t row)
{
    for (size_t col = 0; col < VGA_COLS; col++)
        g_vga_mem[row * VGA_COLS + col] = (u16)(VGA_ATTR << 8);
}

static void vga_scroll(void)
{
    for (size_t row = 1; row < VGA_ROWS; row++)
        for (size_t col = 0; col < VGA_COLS; col++)
            g_vga_mem[(row - 1) * VGA_COLS + col] =
                g_vga_mem[row * VGA_COLS + col];
    vga_clear_row(VGA_ROWS - 1);
    g_vga_row = VGA_ROWS - 1;
    g_vga_col = 0;
}

static void vga_newline(void)
{
    g_vga_col = 0;
    if (++g_vga_row >= VGA_ROWS)
        vga_scroll();
}

static void vga_putcell(char c)
{
    g_vga_mem[g_vga_row * VGA_COLS + g_vga_col] =
        (u16)((VGA_ATTR << 8) | (u8)c);
    if (++g_vga_col >= VGA_COLS)
        vga_newline();
}

static void vga_console_write(struct console_device *dev, char c)
{
    (void)dev;

    if (g_vga_esc) {
        if (c >= '@' && c <= '~')        /* CSI terminator: done */
            g_vga_esc = 0;
        return;                          /* drop escape interior */
    }
    if (c == 0x1B) {
        g_vga_esc = 1;
        return;
    }

    switch (c) {
    case '\n':
        vga_newline();
        break;
    case '\r':
        g_vga_col = 0;
        break;
    case '\b':
        if (g_vga_col > 0)
            g_vga_col--;
        break;
    default:
        if ((u8)c >= 0x20)
            vga_putcell(c);
        break;
    }
    vga_move_cursor();
}

struct console_device g_vga_console = {
    .name = "vga0",
    .write = vga_console_write,
};

void vga_console_init(void)
{
    g_vga_mem = (volatile u16 *)(uintptr_t)phys_to_virt(VGA_TEXT_PHYS);

    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; i++)
        g_vga_mem[i] = (u16)(VGA_ATTR << 8);
    g_vga_col = 0;
    g_vga_row = 0;
    g_vga_esc = 0;
    vga_move_cursor();
}