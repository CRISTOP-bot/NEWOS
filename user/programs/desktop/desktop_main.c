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
#define DESK_MAX_H  1200u

#define CLR_PANEL   0x101b32u
#define CLR_TASKBAR 0x101522u
#define CLR_ACTIVE  0x62d6c8u
#define CLR_ACCENT  0x62d6c8u
#define CLR_WINDOW1 0x245a68u
#define CLR_WINDOW2 0x6553a0u
#define CLR_WINDOW3 0x277c70u
#define CLR_TEXT    0xeaeaeau
#define CLR_TEXT_DIM 0x8a8a9au

/* Compose in userspace and submit one clipped frame. Per-pixel syscalls made
 * dragging a window thousands of trap transitions per frame. */
static u32 canvas[DESK_MAX_W * DESK_MAX_H];
static u64 screen_w, screen_h;
static u32 glyph_buf[20 * 20];
static char bin_entries[4][NSH_NAME_LEN];
static int bin_entry_count;

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
    if (!w || !h || !px || x >= screen_w || y >= screen_h) return;
    if (w > screen_w - x) w = screen_w - x;
    if (h > screen_h - y) h = screen_h - y;
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++)
            canvas[(y + yy) * screen_w + x + xx] = px[yy * stride + xx];
    }
}

static void solid(u64 x, u64 y, u64 w, u64 h, u32 color)
{
    u64 yy, xx;
    if (!w || !h || x >= screen_w || y >= screen_h) return;
    if (w > screen_w - x) w = screen_w - x;
    if (h > screen_h - y) h = screen_h - y;
    for (yy = 0; yy < h; yy++)
        for (xx = 0; xx < w; xx++)
            canvas[(y + yy) * screen_w + x + xx] = color;
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
    u64 i;
    if (x >= screen_w || y >= screen_h) return;
    if (w > screen_w - x) w = screen_w - x;
    for (i = 0; i < w; i++) canvas[y * screen_w + x + i] = color;
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
/* Window indices are z-order slots; identity must move with the window. */
static int WID[MAX_W] = {NSYS, NMOUSE, NABOUT, NFILES};

static void load_bin_entries(void)
{
    struct nsh_dirent entries[16];
    long count = sys_readdir("/bin", entries, sizeof(entries));
    int i;
    bin_entry_count = 0;
    if (count <= 0) return;
    if (count > 16) count = 16;
    for (i = 0; i < count && bin_entry_count < 4; i++) {
        if (!entries[i].name[0]) continue;
        nstrcpy(bin_entries[bin_entry_count], entries[i].name);
        bin_entry_count++;
    }
}

/* FILES is a small launcher: every row names an executable discovered from
 * the live /bin directory, and clicking it runs that program. */
static void launch_bin_entry(int entry)
{
    char path[NSH_MAX_PATH];
    char *argv[3];
    const char *arg = 0;
    long pid;
    if (entry < 0 || entry >= bin_entry_count) return;
    nstrcpy(path, "/bin/");
    nstrcat(path, bin_entries[entry]);
    argv[0] = path;
    if (nstrcmp(bin_entries[entry], "ls") == 0) arg = "/";
    else if (nstrcmp(bin_entries[entry], "img") == 0) arg = "/etc/splash.bmp";
    else if (nstrcmp(bin_entries[entry], "kilo") == 0 ||
             nstrcmp(bin_entries[entry], "nano") == 0) arg = "/etc/motd";
    if (arg) {
        argv[1] = (char *)arg;
        pid = sys_spawn2(path, argv, 2);
    } else {
        pid = sys_spawn2(path, argv, 1);
    }
    if (pid > 0) {
        (void)sys_waitpid((pid_t)pid);
    } else {
        nputs("desktop: unable to launch /bin entry\n");
        sys_sleep(700);
    }
}

static int any_open(void)
{
    unsigned int i;
    for (i = 0; (u64)i < MAX_W; i++)
        if (WOPEN[i]) return 1;
    return 0;
}

static int top_visible(void)
{
    unsigned int i;
    for (i = 0; i < MAX_W; i++)
        if (WOPEN[i] && !WMIN[i]) return (int)i;
    return -1;
}

static void raise_window(int idx)
{
    long x, y, w, h;
    u32 color;
    const char *title;
    int open, minimized, identity, i;
    if (idx <= 0 || idx >= (int)MAX_W) return;
    x = WX[idx]; y = WY[idx]; w = WW[idx]; h = WH[idx];
    color = WTB[idx]; title = WTITLE[idx];
    open = WOPEN[idx]; minimized = WMIN[idx];
    identity = WID[idx];
    for (i = idx; i > 0; i--) {
        WX[i] = WX[i - 1]; WY[i] = WY[i - 1];
        WW[i] = WW[i - 1]; WH[i] = WH[i - 1];
        WTB[i] = WTB[i - 1]; WTITLE[i] = WTITLE[i - 1];
        WOPEN[i] = WOPEN[i - 1]; WMIN[i] = WMIN[i - 1];
        WID[i] = WID[i - 1];
    }
    WX[0] = x; WY[0] = y; WW[0] = w; WH[0] = h;
    WTB[0] = color; WTITLE[0] = title;
    WOPEN[0] = open; WMIN[0] = minimized;
    WID[0] = identity;
}

static void present(u64 w, u64 h)
{
    struct nsh_fbwrite rq;
    rq.x = 0; rq.y = 0; rq.w = w; rq.h = h;
    rq.pixels = (u64)canvas;
    rq.pixlen = w * h * sizeof(canvas[0]);
    (void)sys_fbwrite(&rq);
}

static void draw_window(int i, u64 mem_used, u64 mem_total,
                         long mx, long my, int mb, int mw,
                         int focused, u64 uptime)
{
    long x = WX[i], y = WY[i], w = WW[i], h = WH[i];
    char line[64];
    int p;
    int identity = WID[i];
    u64 v;
    char num[22];
    int ni;

    if (!WOPEN[i] || WMIN[i] || x >= (long)DESK_MAX_W || y >= (long)DESK_MAX_H)
        return;
    if (x + w > DESK_MAX_W) w = DESK_MAX_W - x;
    if (y + h > DESK_MAX_H) h = DESK_MAX_H - y;

    solid((u64)x, (u64)y, (u64)w, (u64)h, 0x10101au);
    solid((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), (u64)(h - 2),
          focused ? 0x1e1e2eu : 0x14141au);
    solid((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), PANEL_H - 1, WTB[i]);
    if (focused) {
        hline((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), CLR_ACTIVE);
        hline((u64)(x + 1), (u64)(y + 2), (u64)(w - 2), CLR_ACTIVE);
    }
    solid((u64)(x + w - 37), (u64)(y + 2), 14, PANEL_H - 3,
          mb == 1 && mx >= x + w - 37 && mx < x + w - 23 &&
          my >= y + 2 && my < y + PANEL_H - 1 ? 0x555566u : 0x222233u);
    text_at("-", (u64)(x + w - 34), (u64)(y + 7), 1u, 0xeeeeeeu, 0x222233u);
    solid((u64)(x + w - 20), (u64)(y + 2), 14, PANEL_H - 3,
          mb == 1 && mx >= x + w - 18 && mx < x + w - 4 &&
          my >= y + 2 && my < y + PANEL_H - 1 ? 0xff3333u : 0x441111u);
    text_at("X", (u64)(x + w - 12), (u64)(y + 6), 2u, 0xeeeeeeu, 0x441111u);
    text_at(WTITLE[i], (u64)(x + 8), (u64)(y + 6), 2u, 0xffffffu, WTB[i]);

    if (identity == NSYS) {
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
        text_at("Drag title. -min X=close.", (u64)(x + 10), (u64)(y + 120), 1u, 0x888888u, 0x10101au);
    } else if (identity == NMOUSE) {
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
    } else if (identity == NABOUT) {
        struct nsh_uname u;
        if (sys_uname(&u) == 0) {
            text_at(u.sysname, (u64)(x + 10), (u64)(y + 36), 2u, 0xe0e0e0u, WTB[i]);
            text_at(u.release, (u64)(x + 10), (u64)(y + 62), 1u, 0xaaaaaau, 0x10101au);
            text_at(u.machine, (u64)(x + 10), (u64)(y + 78), 1u, 0xaaaaaau, 0x10101au);
        } else {
            text_at("uname unavailable", (u64)(x + 10), (u64)(y + 44), 1u, 0xcc8888u, 0x10101au);
        }
        text_at("Framebuffer desktop", (u64)(x + 10), (u64)(y + 98), 1u, 0x888888u, 0x10101au);
    } else if (identity == NFILES) {
        text_at("/bin directory", (u64)(x + 10), (u64)(y + 36), 1u, 0xe0e0e0u, 0x10101au);
        if (!bin_entry_count)
            text_at("Could not read /bin", (u64)(x + 10), (u64)(y + 56), 1u, 0xcc8888u, 0x10101au);
        for (int e = 0; e < bin_entry_count; e++)
            text_at(bin_entries[e], (u64)(x + 10), (u64)(y + 56 + e * 16),
                    1u, 0xaaaaaau, 0x10101au);
    }
}

static void redraw_all(u64 pw, u64 ph, u64 mem_used, u64 mem_total,
                        long mx, long my, int mb, int mw,
                        int focused, u64 uptime)
{
    unsigned int i;
    u64 y;
    /* Deep twilight wallpaper with fine stars. */
    for (y = PANEL_H; y < ph; y++) {
        u32 r = 10u + (u32)(y * 15u / ph);
        u32 g = 20u + (u32)(y * 23u / ph);
        u32 b = 42u + (u32)(y * 35u / ph);
        u32 color = (r << 16) | (g << 8) | b;
        for (i = 0; i < pw; i++) canvas[y * screen_w + i] = color;
    }
    for (i = 0; i < 22; i++) {
        u64 sx = ((u64)i * 97u + 31u) % pw;
        u64 sy = PANEL_H + (((u64)i * 53u + 17u) % (ph - PANEL_H));
        solid(sx, sy, 2, 2, (i % 3u) ? 0x35465au : 0x597080u);
    }
    text_at("NEWOS", 22, ph / 2u - 16u, 4u, 0x314657u, 0x0d1827u);
    text_at("A SMALL SYSTEM WITH ROOM TO GROW", 26, ph / 2u + 10u, 1u,
            0x3f5969u, 0x0d1827u);
    /* taskbar bg */
    solid(0, ph - TASKBAR_H, pw, TASKBAR_H, CLR_TASKBAR);
    hline(0, ph - TASKBAR_H, pw, 0x354252u);
    /* windows back to front */
    for (i = MAX_W; i > 0; i--)
        if (WOPEN[i - 1] && !WMIN[i - 1])
            draw_window(i - 1, mem_used, mem_total, mx, my, mb, mw,
                        (int)(i - 1) == focused, uptime);
    /* taskbar buttons */
    {
        u64 bx = 4;
        for (i = 0; (u64)i < MAX_W; i++) {
            if (!WOPEN[i]) continue;
            u64 bw = 72;
            u32 bg = WMIN[i] ? 0x20202au :
                     ((int)i == focused ? CLR_ACTIVE : 0x2a2a3eu);
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
    hline(0, PANEL_H - 1, pw, 0x354252u);
    solid(8, 6, 15, 15, CLR_ACCENT);
    text_at("N", 11, 10, 2u, 0x101522u, CLR_ACCENT);
    text_at("NEWOS 0.3.0", 29, 8, 1u, 0xffffffu, CLR_PANEL);
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
    text_at("WORKSPACE", pw / 2u - 20u, 10, 1u, 0x9aabba, CLR_PANEL);
    /* cursor */
    cursor_paint(mx, my);
    present(pw, ph);
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
        nputs("Available: ls cat echo hello newpkg vid desktop nano about\n");
        nputs("Framebuffer support is required; returning to shell.\n");
        return 1;
    }
    if (!fi.width || !fi.height || fi.width < 300 || fi.height < 100) {
        nputs("desktop: framebuffer geometry is too small\n");
        return 1;
    }
    u64 pw = fi.width < DESK_MAX_W ? fi.width : DESK_MAX_W;
    u64 ph = fi.height < DESK_MAX_H ? fi.height : DESK_MAX_H;
    screen_w = pw; screen_h = ph;
    {
        long margin = 18;
        long usable_w = (long)pw - 2 * margin;
        long usable_h = (long)ph - (long)PANEL_H - (long)TASKBAR_H - 2 * margin;
        long col = usable_w / 2;
        long row = usable_h / 2;
        if (col < 190) col = usable_w;
        if (row < 105) row = usable_h;
        WW[0] = col > 390 ? 350 : col - 8;
        WW[1] = col > 390 ? 330 : col - 8;
        WW[2] = col > 390 ? 350 : col - 8;
        WW[3] = col > 390 ? 320 : col - 8;
        WH[0] = row > 200 ? 180 : row - 8;
        WH[1] = row > 200 ? 190 : row - 8;
        WH[2] = row > 200 ? 160 : row - 8;
        WH[3] = row > 200 ? 180 : row - 8;
        WX[0] = margin; WY[0] = PANEL_H + margin;
        WX[1] = col > 190 ? margin + col : margin;
        WY[1] = PANEL_H + margin;
        WX[2] = margin; WY[2] = PANEL_H + margin + row;
        WX[3] = col > 190 ? margin + col : margin;
        WY[3] = PANEL_H + margin + row;
        for (int wi = 0; wi < (int)MAX_W; wi++) {
            if (WW[wi] < 64) WW[wi] = 64;
            if (WH[wi] < PANEL_H + 20) WH[wi] = PANEL_H + 20;
            if (WX[wi] + WW[wi] > (long)pw) WX[wi] = (long)pw - WW[wi];
            if (WY[wi] + WH[wi] > (long)ph - TASKBAR_H)
                WY[wi] = (long)ph - TASKBAR_H - WH[wi];
        }
    }
    load_bin_entries();
    nputs("desktop: NEWOS workspace ready\n");

    if (sys_sysinfo(&si) == 0 && si.total_frames) {
        mem_total = si.total_frames * 4096u / (1024u * 1024u);
        u64 mem_free = si.free_frames * 4096u / (1024u * 1024u);
        mem_used = mem_free < mem_total ? mem_total - mem_free : 0;
        uptime = si.uptime_sec;
    }

    redraw_all(pw, ph, mem_used, mem_total, mx, my, mb, mw, focused, uptime);

    for (;;) {
        long nx, ny, px, py;
        int nb, nw, changed = 0, i, on_taskbar = 0;

        if (mouse_poll(&nx, &ny, &nb, &nw, &mseq) == 0) {
            if (nx != mx || ny != my || nb != mb || nw != mw)
                changed = 1;
            mx = nx; my = ny; mb = nb; mw = nw;
        }
        /* Pixel coordinates straight from the driver: no 80x25 scaling. */
        px = mx;
        py = my;

        if ((mb & 1) && !(pmb & 1)) {
            if (px >= 4 && py >= (long)(ph - TASKBAR_H) && py < (long)ph) {
                u64 bx = 4;
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (!WOPEN[i]) continue;
                    if ((u64)px >= bx && (u64)px < bx + 66) {
                        if (i == 0 && !WMIN[i]) {
                            WMIN[i] = 1;
                            focused = -1;
                        } else {
                            WMIN[i] = 0;
                            raise_window(i);
                            focused = 0;
                        }
                        drag = -1;
                        changed = 1;
                        on_taskbar = 1;
                        break;
                    }
                    bx += 72;
                }
                on_taskbar = 1;
            }
            if (!on_taskbar) {
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (!WOPEN[i] || WMIN[i]) continue;
                    if (px >= WX[i] + WW[i] - 20 && px < WX[i] + WW[i] - 6 &&
                        py >= WY[i] + 2 && py < WY[i] + PANEL_H - 1) {
                        WOPEN[i] = 0;
                        if (focused == i) focused = top_visible();
                        drag = -1;
                        changed = 1;
                        on_taskbar = 1;
                        break;
                    }
                    if (px >= WX[i] + WW[i] - 37 && px < WX[i] + WW[i] - 23 &&
                        py >= WY[i] + 2 && py < WY[i] + PANEL_H - 1) {
                        WMIN[i] = 1;
                        if (focused == i) focused = top_visible();
                        drag = -1;
                        changed = 1;
                        on_taskbar = 1;
                        break;
                    }
                }
            }
            if (!on_taskbar) {
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (!WOPEN[i] || WMIN[i]) continue;
                    if (px >= WX[i] && px < WX[i] + WW[i] &&
                        py >= WY[i] && py < WY[i] + PANEL_H) {
                        raise_window(i);
                        focused = 0;
                        drag = 0;
                        grab_ox = px - WX[0]; grab_oy = py - WY[0];
                        changed = 1;
                        break;
                    }
                    if (WID[i] == NFILES && px >= WX[i] + 6 &&
                        px < WX[i] + WW[i] - 6 &&
                        py >= WY[i] + 52 &&
                        py < WY[i] + 56 + bin_entry_count * 16) {
                        int entry = (int)(py - (WY[i] + 56)) / 16;
                        if (entry >= 0 && entry < bin_entry_count) {
                            launch_bin_entry(entry);
                            changed = 1;
                        }
                        on_taskbar = 1;
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
            if (WX[0] > (long)screen_w - 64)
                WX[0] = (long)screen_w - 64;
            if (WY[0] > (long)screen_h - (long)PANEL_H)
                WY[0] = (long)screen_h - (long)PANEL_H;
            changed = 1;
        }

        if (sys_sysinfo(&si) == 0 && si.total_frames) {
            u64 old_uptime = uptime;
            mem_total = si.total_frames * 4096u / (1024u * 1024u);
            u64 mem_free = si.free_frames * 4096u / (1024u * 1024u);
            mem_used = mem_free < mem_total ? mem_total - mem_free : 0;
            uptime = si.uptime_sec;
            if (uptime != old_uptime) changed = 1;
        }

        if (changed || mx != pmx || my != pmy) {
            redraw_all(pw, ph, mem_used, mem_total, mx, my, mb, mw, focused, uptime);
            pmx = mx; pmy = my; pmb = mb;
        }
        if (!any_open()) break;
        sys_sleep(33);
    }
    solid(0, 0, pw, ph, 0x000000u);
    present(pw, ph);
    nputs("desktop: done\n");
    return 0;
}
