/* 07-newpaint: a small framebuffer painting app built entirely with the SDK.
 *
 * Controls (serial console prints them at startup):
 *   left mouse button   paint with the selected colour
 *   right button        erase
 *   wheel               cycle colour
 *   1..8                pick colour
 *   [ / ]               brush size
 *   c                   clear
 *   q or Esc            quit
 *
 * The panel is claimed with SYS_CONSOLE_GUI so the kernel stops painting
 * fbcon text and its legacy cursor over our pixels. The mouse position comes
 * from SYS_MOUSE_GET (device pixels); the drawing surface is a fixed 320x200
 * canvas centred on the panel with a palette strip underneath.
 *
 * The backbuffer is static BSS (no malloc arena) and dirty rows are blitted
 * with SYS_FBWRITE. FBWRITE reads rows contiguously with stride rq.w, so we
 * always send full canvas rows and vary only the row range.
 */
#include <stdio.h>
#include <newos/nsh.h>

#define CANVAS_W  320
#define CANVAS_H  200
#define PALETTE_H 32
#define TOTAL_H   (CANVAS_H + PALETTE_H)
#define BRUSH_MAX 12
#define CURSOR_MAX (BRUSH_MAX + 6)

#define BG        0xff0d1117u
#define PAL_BG    0xff161b22u
#define WHITE     0xffffffffu

static const unsigned int palette[8] = {
    0xff000000u, /* black   */
    0xffffffffu, /* white   */
    0xffe63950u, /* red     */
    0xff44c070u, /* green   */
    0xff4d94ffu, /* blue    */
    0xffe8d44du, /* yellow  */
    0xff3ddbd9u, /* cyan    */
    0xffd16bffu, /* magenta */
};

static unsigned int backbuf[CANVAS_W * TOTAL_H];
static unsigned int cursor_save[CURSOR_MAX * CURSOR_MAX];
static unsigned int clear_row[2048];
static int cursor_x = -1, cursor_y = -1, cursor_size = 0;
static int last_x = -1, last_y = -1, last_inside = 0;
static int brush = 3, color_idx = 0, quitting = 0;
static int origin_x = 0, origin_y = 0;
static int dirty_y0 = TOTAL_H, dirty_y1 = -1;

static void mark(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 >= TOTAL_H) y1 = TOTAL_H - 1;
    if (y0 > y1) return;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

/* Write anywhere in the backbuffer (canvas + palette strip). */
static void raw(int x, int y, unsigned int c)
{
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= TOTAL_H)
        return;
    backbuf[y * CANVAS_W + x] = c;
    mark(y, y);
}

/* Write inside the drawing canvas only. */
static void put(int x, int y, unsigned int c)
{
    if (y >= 0 && y < CANVAS_H)
        raw(x, y, c);
}

static void clear_canvas(void)
{
    for (int y = 0; y < CANVAS_H; ++y)
        for (int x = 0; x < CANVAS_W; ++x)
            backbuf[y * CANVAS_W + x] = BG;
    mark(0, CANVAS_H - 1);
}

static void draw_palette(void)
{
    for (int y = CANVAS_H; y < TOTAL_H; ++y)
        for (int x = 0; x < CANVAS_W; ++x)
            backbuf[y * CANVAS_W + x] = PAL_BG;

    for (int i = 0; i < 8; ++i) {
        int x = 8 + i * 36;
        for (int dy = 0; dy < 20; ++dy)
            for (int dx = 0; dx < 28; ++dx)
                raw(x + dx, CANVAS_H + 4 + dy, palette[i]);
        if (i == color_idx) {
            for (int k = -2; k <= 29; ++k) {
                raw(x + k, CANVAS_H + 2, WHITE);
                raw(x + k, CANVAS_H + 25, WHITE);
            }
            for (int k = 2; k <= 25; ++k) {
                raw(x - 2, CANVAS_H + k, WHITE);
                raw(x + 29, CANVAS_H + k, WHITE);
            }
        }
    }
    mark(CANVAS_H, TOTAL_H - 1);
}

static void paint(int mx, int my, unsigned int c)
{
    for (int dy = 0; dy < brush; ++dy)
        for (int dx = 0; dx < brush; ++dx)
            put(mx - brush / 2 + dx, my - brush / 2 + dy, c);
}

/* Wipe kernel boot messages off the whole panel before drawing the app.
 * FBWRITE copies contiguous rows with stride rq.w, so one 2048-pixel row
 * buffer is enough for any panel width the kernel accepts (<= 4096). */
static void clear_panel(unsigned int w, unsigned int h)
{
    for (unsigned i = 0; i < 2048; ++i)
        clear_row[i] = BG;
    for (unsigned y = 0; y < h; ++y) {
        unsigned x = 0;
        while (x < w) {
            unsigned n = w - x;
            if (n > 2048)
                n = 2048;
            struct nsh_fbwrite rq = {0};
            rq.x = x;
            rq.y = y;
            rq.w = n;
            rq.h = 1;
            rq.pixels = (nsh_u64)(unsigned long)clear_row;
            rq.pixlen = n * 4;
            sys_fbwrite(&rq);
            x += n;
        }
    }
}

static void hide_cursor(void)
{
    if (cursor_x < 0)
        return;
    int half = cursor_size / 2;
    for (int y = 0; y < cursor_size; ++y)
        for (int x = 0; x < cursor_size; ++x)
            raw(cursor_x + x - half, cursor_y + y - half,
                cursor_save[y * CURSOR_MAX + x]);
    cursor_x = -1;
}

static void show_cursor(int mx, int my)
{
    if (mx < 0 || my < 0 || mx >= CANVAS_W || my >= CANVAS_H)
        return;
    int size = brush + 6;
    if (size > CURSOR_MAX) size = CURSOR_MAX;
    int half = size / 2;

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            int px = mx + x - half, py = my + y - half;
            cursor_save[y * CURSOR_MAX + x] =
                (px >= 0 && py >= 0 && px < CANVAS_W && py < CANVAS_H)
                    ? backbuf[py * CANVAS_W + px] : BG;
        }
    for (int k = 0; k < size; ++k) {
        raw(mx + k - half, my - half, WHITE);
        raw(mx + k - half, my + size - 1 - half, WHITE);
        raw(mx - half, my + k - half, WHITE);
        raw(mx + size - 1 - half, my + k - half, WHITE);
    }
    cursor_x = mx;
    cursor_y = my;
    cursor_size = size;
}

static void refresh_cursor(void)
{
    hide_cursor();
    if (last_inside)
        show_cursor(last_x, last_y);
}

static void flush(void)
{
    if (dirty_y1 < dirty_y0)
        return;
    struct nsh_fbwrite rq = {0};
    rq.x = (nsh_u64)origin_x;
    rq.y = (nsh_u64)(origin_y + dirty_y0);
    rq.w = CANVAS_W;
    rq.h = (nsh_u64)(dirty_y1 - dirty_y0 + 1);
    rq.pixels = (nsh_u64)(unsigned long)(backbuf + dirty_y0 * CANVAS_W);
    rq.pixlen = rq.w * rq.h * 4;
    sys_fbwrite(&rq);
    dirty_y0 = TOTAL_H;
    dirty_y1 = -1;
}

static void handle_mouse(const struct nsh_mouse *m)
{
    int mx = (int)m->x - origin_x;
    int my = (int)m->y - origin_y;
    last_inside = mx >= 0 && my >= 0 && mx < CANVAS_W && my < CANVAS_H;
    last_x = mx;
    last_y = my;

    hide_cursor();                       /* restore before painting underneath */
    if (m->wheel > 0) {
        color_idx = (color_idx + 1) & 7;
        draw_palette();
    } else if (m->wheel < 0) {
        color_idx = (color_idx + 7) & 7;
        draw_palette();
    }
    if (last_inside) {
        if (m->buttons & 1)
            paint(mx, my, palette[color_idx]);
        if (m->buttons & 2)
            paint(mx, my, BG);
        show_cursor(mx, my);
    }
}

static void handle_key(unsigned char k)
{
    if (k >= '1' && k <= '8') {
        color_idx = k - '1';
        draw_palette();
        refresh_cursor();
    } else if (k == '[' || k == ']') {
        int next = brush + (k == ']' ? 1 : -1);
        if (next < 1) next = 1;
        if (next > BRUSH_MAX) next = BRUSH_MAX;
        if (next != brush) {
            brush = next;
            refresh_cursor();
        }
    } else if (k == 'c' || k == 'C') {
        hide_cursor();
        clear_canvas();
        refresh_cursor();
    } else if (k == 'q' || k == 'Q' || k == 0x1b) {
        quitting = 1;
    }
}

int main(void)
{
    struct nsh_fbinfo fi;
    if (sys_fbinfo(&fi) != 0 || !fi.present || fi.bpp != 32 ||
        fi.width < CANVAS_W || fi.height < TOTAL_H) {
        printf("NewPaint needs a 32-bpp panel of at least %dx%d.\n"
               "Boot the Limine ISO with gui=1.\n", CANVAS_W, TOTAL_H);
        return 1;
    }

    origin_x = (int)((fi.width - CANVAS_W) / 2);
    origin_y = (int)((fi.height - TOTAL_H) / 2);

    printf("NewPaint %dx%d on %llux%llu panel (origin %d,%d)\n",
           CANVAS_W, CANVAS_H, fi.width, fi.height, origin_x, origin_y);
    puts("mouse L=paint R=erase wheel=colour | 1-8 colour [ ] brush c clear q/Esc quit");

    clear_canvas();
    draw_palette();
    if (sys_console_gui(1) != 0) {
        puts("NewPaint: the panel is already owned by another process.");
        return 1;
    }
    clear_panel((unsigned int)fi.width, (unsigned int)fi.height);
    last_x = CANVAS_W / 2;
    last_y = CANVAS_H / 2;
    last_inside = 1;
    refresh_cursor();
    sys_kbd_grab(1);
    flush();                             /* initial full paint */

    struct nsh_mouse m;
    nsh_u64 last_seq = ~(nsh_u64)0;      /* force one initial cursor sample */
    for (;;) {
        unsigned char keys[64];
        long n = sys_kbd_read(keys, sizeof keys);
        for (long i = 0; i < n; ++i) {
            handle_key(keys[i]);
            if (quitting)
                break;
        }
        if (quitting)
            break;

        if (sys_mouse_get(&m) == 0 && m.seq != last_seq) {
            last_seq = m.seq;
            handle_mouse(&m);
        }
        flush();
        sys_sleep(2);
    }

    hide_cursor();
    sys_kbd_grab(0);
    sys_console_gui(0);
    puts("NewPaint: bye");
    return 0;
}
