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
static int g_vga_esc = 0;        /* inside an escape sequence */
static int g_vga_csi = 0;        /* inside CSI (after ESC [) */
static int g_vga_p[8];           /* CSI numeric parameters */
static int g_vga_np;             /* index of current parameter */
static int g_vga_fg = 7;         /* SGR foreground 0-7 (default light gray) */
static int g_vga_bold;           /* SGR intensity bit */
static int g_vga_rev;            /* SGR reverse video */
static u8 g_vga_utf8_lead = 0;  /* pending UTF-8 lead byte (0 = none) */

/* Latin-1 (U+0080..U+00FF) to CP437 glyph. Unlisted entries fall back to
 * the closest ASCII lookalike so Spanish text renders instead of vanishing.
 * Filled once at init (a static range initializer would trip
 * -Werror=override-init on every specific entry). */
static u8 latin1_to_cp437[128];

static void cp437_init(void)
{
    static int done = 0;
    if (done)
        return;
    for (int i = 0; i < 128; i++)
        latin1_to_cp437[i] = (u8)'?';
    latin1_to_cp437[0xA8 - 0x80] = '"';   /* DIAERESIS mark alone */
    latin1_to_cp437[0xB4 - 0x80] = '\'';  /* ACUTE mark alone */
    latin1_to_cp437[0xC1 - 0x80] = 'A'; latin1_to_cp437[0xC9 - 0x80] = 'E';
    latin1_to_cp437[0xCD - 0x80] = 'I'; latin1_to_cp437[0xD3 - 0x80] = 'O';
    latin1_to_cp437[0xDA - 0x80] = 'U'; latin1_to_cp437[0xD1 - 0x80] = 0xA5;
    latin1_to_cp437[0xC4 - 0x80] = 'A'; latin1_to_cp437[0xCB - 0x80] = 'E';
    latin1_to_cp437[0xCF - 0x80] = 'I'; latin1_to_cp437[0xD6 - 0x80] = 'O';
    latin1_to_cp437[0xC5 - 0x80] = 'A'; latin1_to_cp437[0xC7 - 0x80] = 0x80;
    latin1_to_cp437[0xDC - 0x80] = 0x9A;
    latin1_to_cp437[0xE0 - 0x80] = 'a'; latin1_to_cp437[0xE8 - 0x80] = 'e';
    latin1_to_cp437[0xEC - 0x80] = 'i'; latin1_to_cp437[0xF2 - 0x80] = 'o';
    latin1_to_cp437[0xF9 - 0x80] = 'u'; latin1_to_cp437[0xF1 - 0x80] = 0xA4;
    latin1_to_cp437[0xE4 - 0x80] = 'a'; latin1_to_cp437[0xEB - 0x80] = 'e';
    latin1_to_cp437[0xEF - 0x80] = 'i'; latin1_to_cp437[0xF6 - 0x80] = 'o';
    latin1_to_cp437[0xE5 - 0x80] = 'a'; latin1_to_cp437[0xDF - 0x80] = 0xE1;
    latin1_to_cp437[0xE1 - 0x80] = 0xA0; latin1_to_cp437[0xE9 - 0x80] = 0x82;
    latin1_to_cp437[0xED - 0x80] = 0xA1; latin1_to_cp437[0xF3 - 0x80] = 0xA2;
    latin1_to_cp437[0xFA - 0x80] = 0xA3; latin1_to_cp437[0xE7 - 0x80] = 0x87;
    latin1_to_cp437[0xFC - 0x80] = 0x81; latin1_to_cp437[0xE2 - 0x80] = 0x83;
    latin1_to_cp437[0xEA - 0x80] = 0x88; latin1_to_cp437[0xEE - 0x80] = 0x8C;
    latin1_to_cp437[0xF4 - 0x80] = 0x93; latin1_to_cp437[0xFB - 0x80] = 0x96;
    latin1_to_cp437[0xA1 - 0x80] = 0xAD;  /* INVERTED EXCLAMATION */
    latin1_to_cp437[0xBF - 0x80] = 0xA8;  /* INVERTED QUESTION */
    latin1_to_cp437[0xB7 - 0x80] = 0xFA;  /* MIDDLE DOT */
    latin1_to_cp437[0xBA - 0x80] = 0xA7;  /* MASCULINE ORDINAL */
    latin1_to_cp437[0xAA - 0x80] = 0xA6;  /* FEMININE ORDINAL */
    done = 1;
}

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
    vga_mouse_hide();   /* text moved: the overlay cell is stale */
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
    u8 attr = (u8)((g_vga_rev ? (u8)(g_vga_fg << 4) : g_vga_fg) |
                   (u8)(g_vga_rev ? 0 : (g_vga_bold << 3)));
    g_vga_mem[g_vga_row * VGA_COLS + g_vga_col] =
        (u16)(((u16)attr << 8) | (u8)c);
    if (++g_vga_col >= VGA_COLS)
        vga_newline();
}

static void vga_console_write(struct console_device *dev, char c)
{
    (void)dev;
    u8 b = (u8)c;

    /* UTF-8: the ES keyboard emits 2-byte Latin-1 sequences; decode them
     * to a CP437 glyph so VGA and (UTF-8 clean) serial agree. */
    if (g_vga_utf8_lead) {
        if ((b & 0xC0) == 0x80) {
            u8 cp = (g_vga_utf8_lead == 0xC3) ? (u8)(b + 0x40) : b;
            vga_putcell((char)latin1_to_cp437[cp - 0x80]);
        }
        /* Broken sequence: drop it and resync on the next byte. */
        g_vga_utf8_lead = 0;
        vga_move_cursor();
        return;
    }
    if (b >= 0xC2 && b <= 0xC3) {
        g_vga_utf8_lead = b;
        return;
    }
    if (b >= 0x80) {
        vga_putcell('?');                  /* 3-byte seqs etc: stay visible */
        vga_move_cursor();
        return;
    }

    if (g_vga_esc) {
        if (!g_vga_csi) {
            if (c == '[') {
                int k;
                g_vga_csi = 1;
                g_vga_np = 0;
                for (k = 0; k < 8; k++)
                    g_vga_p[k] = 0;
            } else {
                g_vga_esc = 0;   /* bare ESC: ignore */
            }
            return;
        }
        if (c >= '0' && c <= '9') {  /* CSI parameter */
            g_vga_p[g_vga_np] = g_vga_p[g_vga_np] * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (g_vga_np < 7)
                g_vga_np++;
            return;
        }
        if (c == '?') {
            /* Private prefix (?25l/h): skip to the final byte. */
            return;
        }
        if (c == 'C') {              /* cursor forward (line editor) */
            int n = g_vga_p[0] ? g_vga_p[0] : 1;
            while (n-- > 0) {
                if (++g_vga_col >= VGA_COLS)
                    vga_newline();
            }
        } else if (c == 'H' || c == 'f') {
            /* Home, or row;col position (1-based). */
            if (g_vga_np > 0 || g_vga_p[0]) {
                size_t r = g_vga_p[0] ? (size_t)(g_vga_p[0] - 1) : 0;
                size_t cc = (g_vga_np > 0 && g_vga_p[1]) ?
                            (size_t)(g_vga_p[1] - 1) : 0;
                g_vga_row = (r < VGA_ROWS) ? r : VGA_ROWS - 1;
                g_vga_col = (cc < VGA_COLS) ? cc : VGA_COLS - 1;
            } else {
                g_vga_col = 0;
                g_vga_row = 0;
            }
        } else if (c == 'J') {       /* erase screen (clear) */
            for (size_t r = 0; r < VGA_ROWS; r++)
                vga_clear_row(r);
            g_vga_col = 0;
            g_vga_row = 0;
        } else if (c == 'K') {       /* erase in line */
            size_t a = (g_vga_p[0] == 1) ? 0 : g_vga_col;
            size_t b = (g_vga_p[0] == 1) ? g_vga_col + 1 : VGA_COLS;
            size_t cc;
            if (b > VGA_COLS)
                b = VGA_COLS;
            for (cc = a; cc < b; cc++)
                g_vga_mem[g_vga_row * VGA_COLS + cc] =
                    (u16)(VGA_ATTR << 8);
        } else if (c == 'm') {       /* SGR colors for editors */
            int k, n = g_vga_np + 1, col = -1;
            if (n > 8)
                n = 8;
            for (k = 0; k < n; k++) {
                int v = g_vga_p[k];
                if (v == 0) {
                    g_vga_fg = 7;
                    g_vga_bold = 0;
                    g_vga_rev = 0;
                    col = -1;
                } else if (v == 1) {
                    g_vga_bold = 1;
                } else if (v == 7) {
                    g_vga_rev = 1;
                } else if (v == 22) {
                    g_vga_bold = 0;
                } else if (v == 27) {
                    g_vga_rev = 0;
                } else if (v >= 30 && v <= 37) {
                    col = v - 30;
                } else if (v == 39) {
                    g_vga_fg = 7;
                    col = -1;
                }
            }
            if (col >= 0)
                g_vga_fg = col;
        }
        /* Unknown CSI: dropped. */
        if (c >= '@' && c <= '~')    /* CSI terminator: done */
            g_vga_esc = g_vga_csi = 0;
        vga_move_cursor();
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

/* PS/2 mouse cursor overlay: the cell under the pointer is redrawn with
 * a highlight attribute; the original cell is restored on the next move.
 * Scrolling hides the cursor (the text moved, the saved cell is stale). */
static int g_mouse_x = -1;
static int g_mouse_y = -1;
static u16 g_mouse_saved;
static int g_mouse_shown = 0;

void vga_mouse_hide(void)
{
    if (g_mouse_shown && g_vga_mem && g_mouse_x >= 0 && g_mouse_y >= 0)
        g_vga_mem[(size_t)g_mouse_y * VGA_COLS + (size_t)g_mouse_x] =
            g_mouse_saved;
    g_mouse_shown = 0;
}

void vga_mouse_place(int x, int y, int buttons)
{
    u16 attr;

    if (!g_vga_mem)
        return;
    if (x < 0)
        x = 0;
    if (x >= (int)VGA_COLS)
        x = (int)VGA_COLS - 1;
    if (y < 0)
        y = 0;
    if (y >= (int)VGA_ROWS)
        y = (int)VGA_ROWS - 1;

    vga_mouse_hide();
    g_mouse_x = x;
    g_mouse_y = y;
    g_mouse_saved = g_vga_mem[(size_t)y * VGA_COLS + (size_t)x];

    /* Click feedback through the highlight color. */
    if (buttons & 1)
        attr = 0x9F00;          /* left: bright white on blue */
    else if (buttons & 2)
        attr = 0x4F00;          /* right: bright white on red */
    else if (buttons & 4)
        attr = 0x2F00;          /* middle: bright white on green */
    else
        attr = 0x7000;          /* hover: black on light grey */

    g_vga_mem[(size_t)y * VGA_COLS + (size_t)x] =
        (u16)(attr | (g_mouse_saved & 0x00FF));
    g_mouse_shown = 1;
}

struct console_device g_vga_console = {
    .name = "vga0",
    .write = vga_console_write,
};

void vga_console_init(void)
{
    g_vga_mem = (volatile u16 *)(uintptr_t)phys_to_virt(VGA_TEXT_PHYS);

    cp437_init();
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; i++)
        g_vga_mem[i] = (u16)(VGA_ATTR << 8);
    g_vga_col = 0;
    g_vga_row = 0;
    g_vga_esc = 0;
    g_vga_csi = 0;
    g_vga_utf8_lead = 0;
    g_mouse_shown = 0;
    g_mouse_x = -1;
    g_mouse_y = -1;
    vga_move_cursor();
}