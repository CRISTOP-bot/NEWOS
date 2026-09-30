/* Improved desktop: compositor with taskbar, clock, focus.
 *
 * Features:
 *   - Panel bar at top with OS name, time, uptime
 *   - Taskbar at bottom with window buttons
 *   - Draggable, focusable windows with colored borders
 *   - Close boxes, minimize to taskbar
 *   - Live clock, memory bar, uptime
 *   - Better colors, gradients
 */

#include "../../lib/nshlib.h"

#define PANEL_H     28u
#define TASKBAR_H   24u
#define MAX_W       4u
#define CUR_W       12u
#define CUR_H       12u
#define DESK_MAX_W  2048u

#define CLR_PANEL   0x0f3460u
#define CLR_TASKBAR 0x1a1a2eu
#define CLR_ACTIVE  0xe94560u
#define CLR_ACCENT  0x00d2ffu
#define CLR_WINDOW1 0x1b4332u
#define CLR_WINDOW2 0x2d6a4fu
#define CLR_WINDOW3 0x40916cu
#define CLR_TEXT    0xeaeaeau
#define CLR_TEXT_DIM 0x8a8a9au

static u32 rowbuf[DESK_MAX_W];
static u32 fillrow_buf[DESK_MAX_W];
static u32 glyph_buf[20 * 20];

static const char GCHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             ":/. -+()%[]=<>!@#$%^&*_|";
static const u8 GLYPHS[][5] = {
    {0x7,0x5,0x5,0x5,0x7}, {0x2,0x6,0x2,0x2,0x7}, {0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7}, {0x5,0x5,0x7,0x1,0x1}, {0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7}, {0x7,0x1,0x2,0x2,0x2}, {0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7}, {0x5,0x5,0x2,0x5,0x5}, {0x5,0x5,0x2,0x2,0x2},
    {0x6,0x5,0x6,0x5,0x6}, {0x5,0x5,0x5,0x7,0x5}, {0x5,0x7,0x7,0x5,0x5},
    {0x2,0x0,0x2,0x2,0x2}, {0x6,0x5,0x5,0x5,0x5}, {0x7,0x4,0x6,0x4,0x7},
    {0x2,0x5,0x5,0x5,0x2}, {0x3,0x4,0x2,0x1,0x6}, {0x0,0x2,0x0,0x2,0x0},
    {0x1,0x1,0x2,0x4,0x4}, {0x0,0x0,0x0,0x0,0x2}, {0x0,0x0,0x0,0x0,0x0},
    {0x0,0x0,0x7,0x0,0x0}, {0x0,0x2,0x7,0x2,0x0}, {0x5,0x1,0x2,0x4,0x5},
    {0x1,0x2,0x2,0x2,0x1}, {0x4,0x2,0x2,0x2,0x4},
    {0x7,0x1,0x7,0x1,0x7}, {0x2,0x6,0x2,0x6,0x2}, {0x7,0x5,0x7,0x5,0x7},
    {0x2,0x2,0x7,0x2,0x2}, {0x5,0x4,0x4,0x4,0x5}, {0x7,0x4,0x4,0x4,0x7},
    {0x7,0x1,0x5,0x1,0x7}, {0x7,0x1,0x7,0x1,0x7}, {0x7,0x5,0x7,0x5,0x7},
    {0x7,0x1,0x5,0x1,0x7}, {0x7,0x5,0x7,0x1,0x7}, {0x7,0x4,0x7,0x1,0x7},
    {0x2,0x6,0x2,0x2,0x7}, {0x7,0x1,0x7,0x1,0x7}, {0x7,0x4,0x7,0x4,0x7},
    {0x7,0x1,0x2,0x2,0x2}, {0x5,0x5,0x2,0x5,0x5}, {0x7,0x4,0x7,0x1,0x1},
    {0x7,0x4,0x7,0x1,0x7}, {0x7,0x5,0x7,0x5,0x7}, {0x7,0x1,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x1}, {0x7,0x5,0x7,0x5,0x7},
    {0x5,0x5,0x2,0x2,0x2}, {0x2,0x2,0x2,0x5,0x5}, {0x0,0x7,0x0,0x0,0x0},
    {0x0,0x7,0x0,0x7,0x0}, {0x2,0x2,0x2,0x2,0x2}, {0x5,0x5,0x0,0x5,0x5},
    {0x0,0x0,0x0,0x0,0x0}, {0x5,0x5,0x0,0x5,0x5}, {0x0,0x0,0x0,0x0,0x0},
    {0x0,0x6,0x0,0x6,0x0}, {0x0,0x0,0x7,0x0,0x0}, {0x0,0x1,0x6,0x2,0x0},
    {0x0,0x6,0x2,0x2,0x0}, {0x0,0x6,0x2,0x6,0x0}, {0x0,0x5,0x5,0x5,0x0},
    {0x0,0x6,0x2,0x6,0x0}, {0x0,0x6,0x6,0x6,0x0}, {0x0,0x5,0x7,0x5,0x0},
    {0x0,0x2,0x2,0x2,0x2}, {0x0,0x2,0x2,0x2,0x2}, {0x0,0x6,0x5,0x2,0x0},
    {0x0,0x2,0x5,0x2,0x0}, {0x0,0x7,0x0,0x7,0x0}, {0x0,0x7,0x0,0x7,0x0},
    {0x0,0x6,0x0,0x6,0x0}, {0x0,0x5,0x5,0x5,0x0}, {0x0,0x0,0x7,0x0,0x0},
    {0x0,0x7,0x0,0x0,0x7}, {0x0,0x7,0x0,0x7,0x0}, {0x0,0x5,0x5,0x5,0x0},
    {0x0,0x0,0x7,0x0,0x0}, {0x0,0x0,0x0,0x0,0x0}, {0x0,0x1,0x6,0x2,0x0},
    {0x0,0x6,0x2,0x2,0x0}, {0x0,0x6,0x2,0x6,0x0}, {0x0,0x5,0x5,0x5,0x0},
    {0x0,0x6,0x2,0x6,0x0}, {0x0,0x6,0x6,0x6,0x0}, {0x0,0x5,0x7,0x5,0x0},
    {0x0,0x2,0x2,0x2,0x2}, {0x0,0x2,0x2,0x2,0x2}, {0x0,0x6,0x5,0x2,0x0},
    {0x0,0x2,0x5,0x2,0x0}, {0x0,0x7,0x0,0x7,0x0}, {0x0,0x7,0x0,0x7,0x0},
    {0x0,0x6,0x0,0x6,0x0}, {0x0,0x5,0x5,0x5,0x0},
};

static int gidx(char c)
{
    int i = 0;
    while (GCHARS[i]) {
        if (GCHARS[i] == c) return i;
        i++;
    }
    return 23;
}

static void blit_st(u64 x, u64 y, u64 w, u64 h, const u32 *px, u64 stride)
{
    u64 yy, xx;
    if (!w || !h || x >= DESK_MAX_W || y >= DESK_MAX_W) return;
    if (x + w > DESK_MAX_W) w = DESK_MAX_W - x;
    if (y + h > DESK_MAX_W) h = DESK_MAX_W - y;
    if (w > 2048u) w = 2048u;
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++)
            rowbuf[xx] = px[yy * stride + xx];
        struct nsh_fbwrite rq;
        rq.x = x; rq.y = y + yy; rq.w = w; rq.h = 1;
        rq.pixels = (u64)&rowbuf[0]; rq.pixlen = w * 4u;
        if (sys_fbwrite(&rq) != 0) return;
    }
}

static void solid(u64 x, u64 y, u64 w, u64 h, u32 color)
{
    u64 yy, i;
    if (!w || !h || x >= DESK_MAX_W || y >= DESK_MAX_W) return;
    if (x + w > DESK_MAX_W) w = DESK_MAX_W - x;
    if (y + h > DESK_MAX_W) h = DESK_MAX_W - y;
    if (w > 2048u) w = 2048u;
    for (i = 0; i < w; i++) fillrow_buf[i] = color;
    for (yy = 0; yy < h; yy++) {
        struct nsh_fbwrite rq;
        rq.x = x; rq.y = y + yy; rq.w = w; rq.h = 1;
        rq.pixels = (u64)&fillrow_buf[0]; rq.pixlen = w * 4u;
        if (sys_fbwrite(&rq) != 0) return;
    }
}

static void text_at(const char *s, u64 x, u64 y, u64 sc, u32 fg, u32 bg)
{
    u64 cx = x;
    if (sc < 1) sc = 1;
    if (sc > 4) sc = 4;
    while (*s) {
        int gi = gidx(*s);
        u64 r, c, k, l;
        for (r = 0; r < 5; r++) {
            for (c = 0; c < 3; c++) {
                u32 px = (GLYPHS[gi][r] & (u8)(1u << c)) ? fg : bg;
                for (k = 0; k < sc; k++)
                    for (l = 0; l < sc; l++)
                        glyph_buf[(r * sc + k) * 20u + c * sc + l] = px;
            }
        }
        blit_st(cx, y, 3u * sc, 5u * sc, glyph_buf, 20u);
        cx += 4u * sc;
        s++;
    }
}

static void hline(u64 x, u64 y, u64 w, u32 color)
{
    if (x + w > DESK_MAX_W) w = DESK_MAX_W - x;
    u64 i;
    for (i = 0; i < w; i++) fillrow_buf[i] = color;
    struct nsh_fbwrite rq;
    rq.x = x; rq.y = y; rq.w = w; rq.h = 1;
    rq.pixels = (u64)&fillrow_buf[0]; rq.pixlen = w * 4u;
    sys_fbwrite(&rq);
}

static const u16 CURSOR12[CUR_H] = {
    0x001, 0x003, 0x007, 0x00F, 0x01F, 0x03F,
    0x07F, 0x0FF, 0x01F, 0x01B, 0x031, 0x030
};
static void cursor_paint(long px, long py)
{
    long r, c;
    for (r = 0; r < (long)CUR_H; r++) {
        for (c = 0; c < (long)CUR_W; c++) {
            if (CURSOR12[r] & (u16)(1u << (u16)c)) {
                solid((u64)(px + c + 1), (u64)(py + r + 1), 1u, 1u,
                      0x000000u);
                solid((u64)(px + c), (u64)(py + r), 1u, 1u, 0xFFFFFFu);
            }
        }
    }
}

#define NSYS  0
#define NMOUSE 1
#define NABOUT 2
#define NFILES 3

static const char *WTITLE[] = {"SYS", "MOUSE", "NEWOS", "FILES"};
static u32 WTB[] = {CLR_WINDOW1, CLR_WINDOW2, CLR_WINDOW3, 0x00d2ffu};
static long WX[MAX_W] = {120, 440, 240, 560};
static long WY[MAX_W] = {PANEL_H + 12, 180, 340, 240};
static long WW[MAX_W] = {280, 320, 360, 300};
static long WH[MAX_W] = {160, 190, 150, 170};
static int WOPEN[MAX_W] = {1, 1, 1, 1};
static int WMIN[MAX_W] = {0, 0, 0, 0};

static int any_open(void)
{
    unsigned int i;
    for (i = 0; (u64)i < MAX_W; i++)
        if (WOPEN[i] && !WMIN[i]) return 1;
    return 0;
}

static void draw_window(int i, u64 mem_used, u64 mem_total,
                         long mx, long my, int mb, int mw,
                         int focused, u64 uptime)
{
    long x = WX[i], y = WY[i], w = WW[i], h = WH[i];
    char line[64];
    int p;
    u64 v;
    char num[22];
    int ni;

    if (!WOPEN[i] || WMIN[i] || x >= (long)DESK_MAX_W || y >= (long)DESK_MAX_W)
        return;
    if (x + w > DESK_MAX_W) w = DESK_MAX_W - x;
    if (y + h > DESK_MAX_W) h = DESK_MAX_W - y;

    solid((u64)x, (u64)y, (u64)w, (u64)h, 0x10101au);
    solid((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), (u64)(h - 2),
          focused ? 0x1e1e2eu : 0x14141au);
    solid((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), PANEL_H - 1, WTB[i]);
    if (focused) {
        hline((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), CLR_ACTIVE);
        hline((u64)(x + 1), (u64)(y + 2), (u64)(w - 2), CLR_ACTIVE);
    }
    solid((u64)(x + w - 4 - 14), (u64)(y + 2), 14, PANEL_H - 3,
          mb == 1 && mx >= x + w - 18 && mx < x + w - 4 &&
          my >= y + 2 && my < y + PANEL_H - 1 ? 0xff3333u : 0x441111u);
    text_at("X", (u64)(x + w - 12), (u64)(y + 6), 2u, 0xeeeeeeu, 0x441111u);
    text_at(WTITLE[i], (u64)(x + 8), (u64)(y + 6), 2u, 0xffffffu, WTB[i]);

    if (i == NSYS) {
        solid((u64)(x + 10), (u64)(y + 38), (u64)(w - 20), 14, 0x000000u);
        u64 bw = mem_total ? ((u64)(w - 20) * mem_used) / mem_total : 0;
        if (bw) solid((u64)(x + 10), (u64)(y + 38), bw, 14, CLR_ACCENT);
        p = 0;
        text_at("MEM", (u64)(x + 10), (u64)(y + 32), 1u, CLR_TEXT_DIM, 0x10101au);
        v = mem_used; ni = 0;
        if (!v) line[p++] = '0';
        while (v && ni < 20) { num[ni++] = (char)('0' + v % 10); v /= 10; }
        while (ni--) line[p++] = num[ni];
        line[p++] = '/';
        v = mem_total; ni = 0;
        if (!v) line[p++] = '0';
        while (v && ni < 20) { num[ni++] = (char)('0' + v % 10); v /= 10; }
        while (ni--) line[p++] = num[ni];
        line[p++] = 'M'; line[p] = '\0';
        text_at(line, (u64)(x + 10), (u64)(y + 54), 1u, 0xe0e0e0u, 0x10101au);
        p = 0;
        v = uptime / 3600;
        if (v) { ni = 0; while (v && ni < 10) { num[ni++] = (char)('0' + v % 10); v /= 10; }
                 while (ni--) line[p++] = num[ni]; }
        line[p++] = 'h';
        v = (uptime % 3600) / 60;
        ni = 0; while (v && ni < 10) { num[ni++] = (char)('0' + v % 10); v /= 10; }
        while (ni--) line[p++] = num[ni];
        line[p++] = 'm';
        line[p] = '\0';
        text_at(line, (u64)(x + 10), (u64)(y + 70), 1u, 0xe0e0e0u, 0x10101au);
        text_at("Drag title. X=close.", (u64)(x + 10), (u64)(y + 120), 1u, 0x555555u, 0x10101au);
    } else if (i == NMOUSE) {
        long vals[4] = {mx, my, mb, mw};
        text_at("POS:", (u64)(x + 10), (u64)(y + 32), 1u, CLR_TEXT_DIM, 0x10101au);
        p = 0; line[p++] = '(';
        { char n2[12]; int j, nd; v = vals[0] < 0 ? -vals[0] : vals[0]; nd = 0;
          if (!v) n2[nd++] = '0';
          while (v && nd < 10) { n2[nd++] = (char)('0' + v % 10); v /= 10; }
          for (j = nd - 1; j >= 0; j--) line[p++] = n2[j]; }
        line[p++] = ','; line[p++] = ' ';
        { char n2[12]; int j, nd; v = vals[1] < 0 ? -vals[1] : vals[1]; nd = 0;
          if (!v) n2[nd++] = '0';
          while (v && nd < 10) { n2[nd++] = (char)('0' + v % 10); v /= 10; }
          for (j = nd - 1; j >= 0; j--) line[p++] = n2[j]; }
        line[p++] = ')'; line[p] = '\0';
        text_at(line, (u64)(x + 48), (u64)(y + 32), 1u, 0xe0e0e0u, 0x10101au);
        text_at("BTN:", (u64)(x + 10), (u64)(y + 48), 1u, CLR_TEXT_DIM, 0x10101au);
        { char n2[4]; int j; for (j = 0; j < 3; j++) {
            n2[j] = (vals[2] & (1 << j)) ? '1' : '0'; } n2[3] = '\0';
          text_at(n2, (u64)(x + 48), (u64)(y + 48), 1u, 0xe0e0e0u, 0x10101au); }
        text_at("WHEEL:", (u64)(x + 10), (u64)(y + 64), 1u, CLR_TEXT_DIM, 0x10101au);
        p = 0;
        if (vals[3] > 0) { line[p++] = '+'; }
        else if (vals[3] < 0) { line[p++] = '-'; }
        v = vals[3] < 0 ? -vals[3] : vals[3]; ni = 0;
        if (!v) line[p++] = '0';
        while (v && ni < 10) { num[ni++] = (char)('0' + v % 10); v /= 10; }
        while (ni--) line[p++] = num[ni];
        line[p] = '\0';
        text_at(line, (u64)(x + 68), (u64)(y + 64), 1u, 0xe0e0e0u, 0x10101au);
    } else if (i == NABOUT) {
        text_at("NEWOS 0.3.0-gui", (u64)(x + 10), (u64)(y + 32), 2u, 0xe0e0e0u, WTB[i]);
        text_at("X86-64 / GUI Desktop", (u64)(x + 10), (u64)(y + 56), 1u, 0x888888u, 0x10101au);
        text_at("Kernel 64-bit", (u64)(x + 10), (u64)(y + 72), 1u, 0x888888u, 0x10101au);
        text_at("Frame Buffer: Active", (u64)(x + 10), (u64)(y + 88), 1u, 0x888888u, 0x10101au);
    } else if (i == NFILES) {
        text_at("/bin/  /etc/  /dev/  /tmp/", (u64)(x + 10), (u64)(y + 32), 1u, 0xe0e0e0u, 0x10101au);
        text_at("initramfs mounted", (u64)(x + 10), (u64)(y + 50), 1u, 0x888888u, 0x10101au);
        text_at("32 tools available", (u64)(x + 10), (u64)(y + 66), 1u, 0x888888u, 0x10101au);
        text_at("splash.bmp  test.ppm", (u64)(x + 10), (u64)(y + 82), 1u, 0x888888u, 0x10101au);
    }
}

static void draw_panel(u64 pw, u64 uptime)
{
    solid(0, 0, pw, PANEL_H, CLR_PANEL);
    hline(0, PANEL_H - 1, pw, 0x333355u);
    text_at("NEWOS 0.3.0", 6, 6, 2u, 0xffffffu, CLR_PANEL);
    {
        char n2[4]; u64 v = uptime; u64 h = v / 3600; v %= 3600;
        u64 m = v / 60; u64 s = v % 60;
        n2[0] = (char)('0' + h / 10); n2[1] = (char)('0' + h % 10); n2[2] = '\0';
        text_at(n2, pw - 58, 10, 1u, 0xaaaaaau, CLR_PANEL);
        text_at(":", pw - 46, 10, 1u, 0xaaaaaau, CLR_PANEL);
        n2[0] = (char)('0' + m / 10); n2[1] = (char)('0' + m % 10); n2[2] = '\0';
        text_at(n2, pw - 38, 10, 1u, 0xaaaaaau, CLR_PANEL);
        text_at(":", pw - 28, 10, 1u, 0xaaaaaau, CLR_PANEL);
        n2[0] = (char)('0' + s / 10); n2[1] = (char)('0' + s % 10); n2[2] = '\0';
        text_at(n2, pw - 20, 10, 1u, 0xaaaaaau, CLR_PANEL);
    }
    hline(pw / 2 - 30, PANEL_H / 2 - 1, 60, CLR_ACCENT);
}

static void redraw_all(u64 pw, u64 ph, u64 mem_used, u64 mem_total,
                        long mx, long my, int mb, int mw,
                        int focused, u64 uptime)
{
    unsigned int i;
    u64 y;
    for (y = PANEL_H; y < ph; y++) {
        u64 px = (y * 16u) / ph;
        u32 b = (u32)(48u + (y * 96u) / ph);
        u32 color = (px << 16) | (16u << 8) | b;
        for (i = 0; i < pw && i < DESK_MAX_W; i++)
            rowbuf[i] = color;
        struct nsh_fbwrite rq;
        rq.x = 0; rq.y = y; rq.w = pw; rq.h = 1;
        rq.pixels = (u64)&rowbuf[0]; rq.pixlen = pw * 4u;
        sys_fbwrite(&rq);
    }
    /* taskbar bg */
    solid(0, ph - TASKBAR_H, pw, TASKBAR_H, CLR_TASKBAR);
    hline(0, ph - TASKBAR_H, pw, 0x333355u);
    /* windows back to front */
    for (i = MAX_W; i > 0; i--)
        if (WOPEN[i - 1] && !WMIN[i - 1])
            draw_window(i - 1, mem_used, mem_total, mx, my, mb, mw, focused, uptime);
    /* taskbar buttons */
    {
        u64 bx = 4;
        for (i = 0; (u64)i < MAX_W; i++) {
            if (!WOPEN[i] || WMIN[i]) continue;
            u64 bw = 60;
            u32 bg = ((unsigned int)i == (unsigned int)focused) ? CLR_ACTIVE : 0x2a2a3eu;
            solid(bx, ph - TASKBAR_H + 3, bw - 6, TASKBAR_H - 6, bg);
            text_at(WTITLE[i], bx + 3, ph - TASKBAR_H + 8, 1u,
                    0xccccccu, bg);
            bx += bw;
        }
    }
    /* clock */
    {
        struct nsh_time t;
        if (sys_gettime(&t) == 0) {
            char n2[4];
            { n2[0] = (char)('0' + t.hour / 10); n2[1] = (char)('0' + t.hour % 10); n2[2] = '\0';
              text_at(n2, pw - 70, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR); }
            text_at(":", pw - 52, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR);
            { n2[0] = (char)('0' + t.min / 10); n2[1] = (char)('0' + t.min % 10); n2[2] = '\0';
              text_at(n2, pw - 44, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR); }
            text_at(" ", pw - 34, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR);
            { n2[0] = (char)('0' + t.mon / 10); n2[1] = (char)('0' + t.mon % 10); n2[2] = '\0';
              text_at(n2, pw - 26, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR); }
            text_at("/", pw - 16, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR);
            { n2[0] = (char)('0' + t.day / 10); n2[1] = (char)('0' + t.day % 10); n2[2] = '\0';
              text_at(n2, pw - 8, ph - TASKBAR_H + 8, 1u, 0xaaaaaau, CLR_TASKBAR); }
        }
    }
    /* panel */
    solid(0, 0, pw, PANEL_H, CLR_PANEL);
    hline(0, PANEL_H - 1, pw, 0x333355u);
    text_at("NEWOS 0.3.0", 6, 6, 2u, 0xffffffu, CLR_PANEL);
    {
        char n2[4]; u64 v = uptime; u64 h = v / 3600; v %= 3600;
        u64 m = v / 60; u64 s = v % 60;
        n2[0] = (char)('0' + h / 10); n2[1] = (char)('0' + h % 10); n2[2] = '\0';
        text_at(n2, pw - 58, 10, 1u, 0xaaaaaau, CLR_PANEL);
        text_at(":", pw - 46, 10, 1u, 0xaaaaaau, CLR_PANEL);
        n2[0] = (char)('0' + m / 10); n2[1] = (char)('0' + m % 10); n2[2] = '\0';
        text_at(n2, pw - 38, 10, 1u, 0xaaaaaau, CLR_PANEL);
        text_at(":", pw - 28, 10, 1u, 0xaaaaaau, CLR_PANEL);
        n2[0] = (char)('0' + s / 10); n2[1] = (char)('0' + s % 10); n2[2] = '\0';
        text_at(n2, pw - 20, 10, 1u, 0xaaaaaau, CLR_PANEL);
    }
    hline(pw / 2 - 30, PANEL_H / 2 - 1, 60, CLR_ACCENT);
    /* cursor */
    cursor_paint(mx, my);
}

static int mouse_poll(long *x, long *y, int *b, int *w, u64 *seq)
{
    struct nsh_mouse m;

    if (sys_mouse_get(&m) != 0)
        return -1;
    if (m.seq == *seq)
        return -1;                 /* no new hardware packet */
    *seq = m.seq;
    *x = (long)m.x; *y = (long)m.y;
    *b = (int)m.buttons; *w = (int)m.wheel;
    return 0;
}

int main(int argc, char **argv)
{
    struct nsh_fbinfo fi;
    struct nsh_sysinfo si;
    long mx = -1, my = -1, pmx = -2, pmy = -2;
    int mb = 0, pmb = 0, mw = 0;
    u64 mseq = 0;
    int drag = -1;
    long grab_ox = 0, grab_oy = 0;
    int focused = 0;
    u64 mem_used = 1, mem_total = 1, uptime = 0;
    int has_fb = 0;

    (void)argc; (void)argv;
    if (sys_fbinfo(&fi) == 0 && fi.present) {
        has_fb = 1;
    }
    if (!has_fb) {
        nputs("desktop: no framebuffer - text mode\n");
        nputs("NEWOS 0.3.0-gui - Text Desktop\n");
        nputs("Type 'help' for commands. Desktop features require Limine boot.\n");
        nputs("Available: ls cat echo hello newpkg vid desktop about\n");
        nputs("Type 'exit' or wait 5s.\n");
        sys_sleep(5000);
        return 0;
    }
    u64 pw = fi.width < DESK_MAX_W ? fi.width : DESK_MAX_W;
    u64 ph = fi.height;
    nputs("desktop: improved GUI ready\n");

    draw_panel(pw, 0);
    redraw_all(pw, ph, mem_used, mem_total, mx, my, mb, mw, focused, uptime);

    for (;;) {
        long nx, ny, px, py;
        int nb, nw, changed = 0, i;

        if (mouse_poll(&nx, &ny, &nb, &nw, &mseq) == 0) {
            if (nx != mx || ny != my || nb != mb || nw != mw)
                changed = 1;
            mx = nx; my = ny; mb = nb; mw = nw;
        }
        /* Pixel coordinates straight from the driver: no 80x25 scaling. */
        px = mx;
        py = my;

        if ((mb & 1) && !(pmb & 1)) {
            for (i = 0; (u64)i < MAX_W; i++) {
                if (!WOPEN[i] || WMIN[i]) continue;
                if (px >= WX[i] + WW[i] - 18 && px < WX[i] + WW[i] - 4 &&
                    py >= WY[i] + 2 && py < WY[i] + PANEL_H - 1) {
                    WMIN[i] = 1;
                    drag = -1;
                    changed = 1;
                    break;
                }
            }
            if (drag == -1) {
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (!WOPEN[i] || WMIN[i]) continue;
                    if (px >= WX[i] && px < WX[i] + WW[i] &&
                        py >= WY[i] && py < WY[i] + PANEL_H) {
                        long tx = WX[i], ty = WY[i], tw = WW[i], th = WH[i];
                        u32 tc = WTB[i];
                        const char *tt = WTITLE[i];
                        int j;
                        for (j = i; j > 0; j--) {
                            WX[j] = WX[j-1]; WY[j] = WY[j-1];
                            WW[j] = WW[j-1]; WH[j] = WH[j-1];
                            WOPEN[j] = WOPEN[j-1]; WTB[j] = WTB[j-1];
                            WTITLE[j] = WTITLE[j-1]; WMIN[j] = WMIN[j-1];
                        }
                        WX[0] = tx; WY[0] = ty; WW[0] = tw;
                        WH[0] = th; WOPEN[0] = 1; WTB[0] = tc;
                        WTITLE[0] = tt; WMIN[0] = 0;
                        focused = 0;
                        drag = 0;
                        grab_ox = px - WX[0]; grab_oy = py - WY[0];
                        changed = 1;
                        break;
                    }
                }
            }
        }
        if (!(mb & 1)) drag = -1;
        if (drag == 0) {
            WX[0] = px - grab_ox;
            WY[0] = py - grab_oy;
            if (WX[0] < 0) WX[0] = 0;
            if (WY[0] < PANEL_H) WY[0] = PANEL_H;
            changed = 1;
        }

        if ((mb & 1) && !(pmb & 1)) {
u64 bx = 4;
            for (i = 0; (u64)i < MAX_W; i++) {
                if (!WOPEN[i] || WMIN[i]) continue;
                u64 bw = 60;
                if ((u64)mx >= bx && (u64)mx < bx + bw &&
                    (u64)my >= ph - TASKBAR_H && (u64)my < ph) {
                    if (WMIN[i]) { WMIN[i] = 0; changed = 1; }
                    { long tx = WX[i], ty = WY[i], tw = WW[i], th = WH[i];
                      u32 tc = WTB[i]; const char *tt = WTITLE[i];
                      int j;
                      for (j = i; j > 0; j--) {
                          WX[j] = WX[j-1]; WY[j] = WY[j-1];
                          WW[j] = WW[j-1]; WH[j] = WH[j-1];
                          WOPEN[j] = WOPEN[j-1]; WTB[j] = WTB[j-1];
                          WTITLE[j] = WTITLE[j-1]; WMIN[j] = WMIN[j-1];
                      }
                      WX[0] = tx; WY[0] = ty; WW[0] = tw;
                      WH[0] = th; WOPEN[0] = 1; WTB[0] = tc;
                      WTITLE[0] = tt; WMIN[0] = 0;
                      focused = 0;
                      changed = 1;
                    }
                    break;
                }
                bx += bw;
            }
        }
        if (!(mb & 1)) drag = -1;

        if (sys_sysinfo(&si) == 0 && si.total_frames) {
            mem_total = si.total_frames * 4096u / (1024u * 1024u);
            mem_used = mem_total - si.free_frames * 4096u / (1024u * 1024u);
            uptime = si.uptime_sec;
        }

        if (changed || mx != pmx || my != pmy) {
            redraw_all(pw, ph, mem_used, mem_total, mx, my, mb, mw, focused, uptime);
            pmx = mx; pmy = my; pmb = mb;
        }
        if (!any_open()) break;
        sys_sleep(33);
    }
    solid(0, 0, pw, ph, 0x000000u);
    nputs("desktop: done\n");
    return 0;
}
