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
#define TASK_CLOCK_W 80u
#define MAX_W       4u
#define CUR_W       12u
#define CUR_H       12u
#define DESK_MAX_W  2048u
#define DESK_MAX_H  1200u
#define BIN_VISIBLE 8u
#define BIN_MAX     64u

#define CLR_PANEL   0x101b32u
#define CLR_TASKBAR 0x101522u
#define CLR_ACTIVE  0x62d6c8u
#define CLR_ACCENT  0x62d6c8u
#define CLR_WINDOW1 0x245a68u
#define CLR_WINDOW2 0x6553a0u
#define CLR_WINDOW3 0x277c70u
#define CLR_TEXT    0xeaeaeau
#define CLR_TEXT_DIM 0x8a8a9au
#define CLR_BODY    0x171d29u
#define CLR_BORDER  0x080d16u
#define CLR_BORDER_FOCUS 0x62d6c8u
#define CLR_TITLE_BUTTON 0x263449u
#define CLR_CLOSE_BUTTON 0x562b35u
#define CLR_DANGER  0xc94757u
#define CLR_TASK_OPEN 0x263247u
#define CLR_TASK_MIN 0x1a202bu
#define CLR_TASK_EDGE 0x48566au

/* Compose in userspace and submit one clipped frame. Per-pixel syscalls made
 * dragging a window thousands of trap transitions per frame. */
static u32 canvas[DESK_MAX_W * DESK_MAX_H];
static u64 screen_w, screen_h;
static u64 clip_left, clip_top, clip_right, clip_bottom;
static int clip_enabled;
static u32 glyph_buf[20 * 20];
static char bin_entries[BIN_MAX][NSH_NAME_LEN];
static int bin_entry_count;
static int bin_scroll;
static struct nsh_uname desktop_uname;
static int desktop_have_uname;
static char panel_label[96] = "NEWOS";

/* Each 3-bit row is stored MSB-first: bit 2 is the left pixel.
 * Keep one unique GCHARS entry for every GLYPHS row; '?' is the fallback. */
static const char GCHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz:/. -+()%[]=<>!@#$^&*_|,?";
static const u8 GLYPHS[][5] = {
    /* 0-9 */
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
    {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2},
    {7,5,7,5,7}, {7,5,7,1,7},
    /* A-Z */
    {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6},
    {7,4,6,4,7}, {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5},
    {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7},
    {5,7,7,5,5}, {5,7,7,7,5}, {2,5,5,5,2}, {6,5,6,4,4},
    {2,5,5,3,1}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2},
    {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5},
    {5,5,2,2,2}, {7,1,2,4,7},
    /* a-z */
    {0,2,5,7,5}, {4,4,6,5,6}, {0,3,4,4,3}, {1,1,3,5,3},
    {0,2,5,6,3}, {1,2,7,2,2}, {3,5,3,1,6}, {4,4,6,5,5},
    {2,0,2,2,2}, {1,0,1,5,2}, {4,4,5,6,5}, {6,2,2,2,7},
    {0,5,7,5,5}, {0,6,5,5,5}, {0,2,5,5,2}, {6,5,6,4,4},
    {3,5,3,1,1}, {0,6,4,4,4}, {0,3,4,2,6}, {2,7,2,2,3},
    {0,5,5,5,3}, {0,5,5,5,2}, {0,5,5,7,5}, {0,5,2,2,5},
    {0,5,3,1,6}, {0,7,1,2,7},
    /* punctuation */
    {0,2,0,2,0}, {1,1,2,4,4}, {0,0,0,0,2}, {0,0,0,0,0},
    {0,0,7,0,0}, {0,2,7,2,0}, {1,2,4,2,1}, {4,2,1,2,4},
    {5,1,2,4,5}, {6,4,4,4,6}, {3,1,1,1,3}, {0,7,0,7,0},
    {1,2,4,2,1}, {4,2,1,2,4}, {2,2,2,0,2}, {7,5,7,4,3},
    {5,7,5,7,5}, {2,7,6,3,7}, {2,5,0,0,0}, {2,5,2,5,3},
    {0,5,2,5,0}, {0,0,0,0,7}, {2,2,2,2,2}, {0,2,4,0,0},
    {7,1,2,0,2},
};

static int gidx(char c)
{
    int i = 0;
    while (GCHARS[i]) {
        if (GCHARS[i] == c) return i;
        i++;
    }
    return (int)sizeof(GCHARS) - 2; /* final glyph is '?' */
}

static void blit_st(u64 x, u64 y, u64 w, u64 h, const u32 *px, u64 stride)
{
    u64 yy, xx, src_x = 0, src_y = 0;
    if (!w || !h || !px || x >= screen_w || y >= screen_h) return;
    if (w > screen_w - x) w = screen_w - x;
    if (h > screen_h - y) h = screen_h - y;
    if (clip_enabled) {
        if (x >= clip_right || y >= clip_bottom) return;
        if (x < clip_left) {
            src_x = clip_left - x;
            if (src_x >= w) return;
            x = clip_left;
            w -= src_x;
        }
        if (y < clip_top) {
            src_y = clip_top - y;
            if (src_y >= h) return;
            y = clip_top;
            h -= src_y;
        }
        if (w > clip_right - x) w = clip_right - x;
        if (h > clip_bottom - y) h = clip_bottom - y;
    }
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++)
            canvas[(y + yy) * screen_w + x + xx] =
                px[(src_y + yy) * stride + src_x + xx];
    }
}

static void solid(u64 x, u64 y, u64 w, u64 h, u32 color)
{
    u64 yy, xx;
    if (!w || !h || x >= screen_w || y >= screen_h) return;
    if (w > screen_w - x) w = screen_w - x;
    if (h > screen_h - y) h = screen_h - y;
    if (clip_enabled) {
        if (x >= clip_right || y >= clip_bottom) return;
        if (x < clip_left) {
            u64 delta = clip_left - x;
            if (delta >= w) return;
            x = clip_left;
            w -= delta;
        }
        if (y < clip_top) {
            u64 delta = clip_top - y;
            if (delta >= h) return;
            y = clip_top;
            h -= delta;
        }
        if (w > clip_right - x) w = clip_right - x;
        if (h > clip_bottom - y) h = clip_bottom - y;
    }
    for (yy = 0; yy < h; yy++)
        for (xx = 0; xx < w; xx++)
            canvas[(y + yy) * screen_w + x + xx] = color;
}

static void text_at_width(const char *s, u64 x, u64 y, u64 sc,
                          u32 fg, u32 bg, u64 max_w)
{
    u64 cx = x;
    if (sc < 1) sc = 1;
    if (sc > 4) sc = 4;
    while (*s && cx - x + 3u * sc <= max_w) {
        int gi = gidx(*s);
        u64 r, c, k, l;
        for (r = 0; r < 5; r++) {
            for (c = 0; c < 3; c++) {
                u32 px = (GLYPHS[gi][r] & (u8)(1u << (2u - c))) ? fg : bg;
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

static void text_at(const char *s, u64 x, u64 y, u64 sc, u32 fg, u32 bg)
{
    text_at_width(s, x, y, sc, fg, bg, x < screen_w ? screen_w - x : 0);
}

static void hline(u64 x, u64 y, u64 w, u32 color)
{
    u64 i;
    if (x >= screen_w || y >= screen_h) return;
    if (w > screen_w - x) w = screen_w - x;
    if (clip_enabled) {
        if (x >= clip_right || y < clip_top || y >= clip_bottom) return;
        if (x < clip_left) {
            u64 delta = clip_left - x;
            if (delta >= w) return;
            x = clip_left;
            w -= delta;
        }
        if (w > clip_right - x) w = clip_right - x;
    }
    for (i = 0; i < w; i++) canvas[y * screen_w + x + i] = color;
}

static void vline(u64 x, u64 y, u64 h, u32 color)
{
    u64 i;
    if (x >= screen_w || y >= screen_h) return;
    if (h > screen_h - y) h = screen_h - y;
    if (clip_enabled) {
        if (y >= clip_bottom || x < clip_left || x >= clip_right) return;
        if (y < clip_top) {
            u64 delta = clip_top - y;
            if (delta >= h) return;
            y = clip_top;
            h -= delta;
        }
        if (h > clip_bottom - y) h = clip_bottom - y;
    }
    for (i = 0; i < h; i++) canvas[(y + i) * screen_w + x] = color;
}

static char *append_number(char *dst, u64 value)
{
    char digits[20];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < (int)sizeof(digits));
    while (n) *dst++ = digits[--n];
    return dst;
}

static char *append_two_digits(char *dst, u64 value)
{
    *dst++ = (char)('0' + (value / 10u) % 10u);
    *dst++ = (char)('0' + value % 10u);
    return dst;
}

static const u16 CURSOR12[CUR_H] = {
    0x001, 0x003, 0x007, 0x00F, 0x01F, 0x03F,
    0x07F, 0x0FF, 0x01F, 0x01B, 0x031, 0x030
};
static void cursor_paint(long px, long py)
{
    long r, c;
    if (px < 0 || py < 0 || (u64)px >= screen_w || (u64)py >= screen_h)
        return;
    /* Paint the offset outline first; interleaving outline and fill lets a
     * later white pixel erase the previous pixel's one-pixel shadow. */
    for (r = 0; r < (long)CUR_H; r++) {
        for (c = 0; c < (long)CUR_W; c++) {
            if (CURSOR12[r] & (u16)(1u << (u16)c)) {
                long sx = px + c + 1, sy = py + r + 1;
                if ((u64)sx < screen_w && (u64)sy < screen_h)
                    solid((u64)sx, (u64)sy, 1u, 1u, 0x000000u);
            }
        }
    }
    for (r = 0; r < (long)CUR_H; r++) {
        for (c = 0; c < (long)CUR_W; c++) {
            if (CURSOR12[r] & (u16)(1u << (u16)c)) {
                long sx = px + c, sy = py + r;
                if ((u64)sx < screen_w && (u64)sy < screen_h)
                    solid((u64)sx, (u64)sy, 1u, 1u, 0xFFFFFFu);
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

static u64 open_window_count(void)
{
    u64 i, count = 0;
    for (i = 0; i < MAX_W; i++)
        if (WOPEN[i]) count++;
    return count;
}

static u64 taskbar_slot_width(u64 width)
{
    u64 count = open_window_count();
    u64 clock_x = width > TASK_CLOCK_W + 8u ? width - TASK_CLOCK_W : 8u;
    u64 available = clock_x > 8u ? clock_x - 8u : width;
    return count ? available / count : 0;
}

static void load_bin_entries(void)
{
    struct nsh_dirent entries[64];
    long count = sys_readdir("/bin", entries, sizeof(entries));
    int i;
    bin_entry_count = 0;
    if (count <= 0) return;
    if (count > 64) count = 64;
    for (i = 0; i < count && bin_entry_count < (int)BIN_MAX; i++) {
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
    int min_hover, close_hover;
    u32 title_color, body_color = focused ? 0x20293a : 0x171d29;
    u64 v;
    char num[22];
    int ni;

    if (!WOPEN[i] || WMIN[i] || x < 0 || y < 0 ||
        x >= (long)screen_w || y >= (long)screen_h)
        return;
    if (x + w > (long)screen_w) w = (long)screen_w - x;
    if (y + h > (long)screen_h) h = (long)screen_h - y;
    if (w < 40 || h < (long)PANEL_H) return;
    clip_left = (u64)x;
    clip_top = (u64)y;
    clip_right = (u64)(x + w);
    clip_bottom = (u64)(y + h);
    clip_enabled = 1;

    solid((u64)x, (u64)y, (u64)w, (u64)h,
          focused ? CLR_BORDER_FOCUS : CLR_BORDER);
    solid((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), (u64)(h - 2),
          body_color);
    title_color = WTB[i];
    if (!focused)
        title_color = (((title_color & 0xfefefeu) >> 1) + 0x202020u);
    solid((u64)(x + 1), (u64)(y + 1), (u64)(w - 2), PANEL_H - 1,
          title_color);
    if (focused) {
        hline((u64)x, (u64)y, (u64)w, CLR_BORDER_FOCUS);
    }
    vline((u64)x, (u64)y, (u64)h, focused ? CLR_BORDER_FOCUS : CLR_BORDER);
    vline((u64)(x + w - 1), (u64)y, (u64)h,
          focused ? CLR_BORDER_FOCUS : CLR_BORDER);
    min_hover = mx >= x + w - 37 && mx < x + w - 23 &&
                my >= y + 2 && my < y + PANEL_H - 1;
    close_hover = mx >= x + w - 20 && mx < x + w - 6 &&
                  my >= y + 2 && my < y + PANEL_H - 1;
    solid((u64)(x + w - 37), (u64)(y + 2), 14, PANEL_H - 3,
          min_hover ? ((mb & 1) ? CLR_ACTIVE : 0x3d5966u) : CLR_TITLE_BUTTON);
    text_at("-", (u64)(x + w - 34), (u64)(y + 7), 2u,
            min_hover && (mb & 1) ? CLR_PANEL : CLR_TEXT,
            min_hover ? ((mb & 1) ? CLR_ACTIVE : 0x3d5966u) : CLR_TITLE_BUTTON);
    solid((u64)(x + w - 20), (u64)(y + 2), 14, PANEL_H - 3,
          close_hover ? ((mb & 1) ? 0x9c283d : CLR_DANGER) : CLR_CLOSE_BUTTON);
    text_at("X", (u64)(x + w - 17), (u64)(y + 7), 2u,
            CLR_TEXT, close_hover ? CLR_DANGER : CLR_CLOSE_BUTTON);
    text_at(WTITLE[i], (u64)(x + 8), (u64)(y + 7), 2u,
            0xffffffu, title_color);

    if (identity == NSYS) {
        text_at("MEM", (u64)(x + 10), (u64)(y + 32), 2u,
                CLR_TEXT_DIM, body_color);
        solid((u64)(x + 10), (u64)(y + 47), (u64)(w - 20), 10, 0x080d16u);
        u64 bw = mem_total ? ((u64)(w - 20) * mem_used) / mem_total : 0;
        if (bw) solid((u64)(x + 10), (u64)(y + 47), bw, 10, CLR_ACCENT);
        p = 0;
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
        text_at(line, (u64)(x + 10), (u64)(y + 62), 2u, CLR_TEXT, body_color);
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
        text_at(line, (u64)(x + 10), (u64)(y + 82), 2u, CLR_TEXT, body_color);
        text_at("Drag title. -min X=close.", (u64)(x + 10),
                (u64)(y + 128), 2u, CLR_TEXT_DIM, body_color);
    } else if (identity == NMOUSE) {
        long vals[4] = {mx, my, mb, mw};
        text_at("POS:", (u64)(x + 10), (u64)(y + 34), 2u, CLR_TEXT_DIM, body_color);
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
        text_at(line, (u64)(x + 62), (u64)(y + 34), 2u, CLR_TEXT, body_color);
        text_at("BTN:", (u64)(x + 10), (u64)(y + 56), 2u, CLR_TEXT_DIM, body_color);
        { char n2[4]; int j; for (j = 0; j < 3; j++) {
            n2[j] = (vals[2] & (1 << j)) ? '1' : '0'; } n2[3] = '\0';
          text_at(n2, (u64)(x + 62), (u64)(y + 56), 2u, CLR_TEXT, body_color); }
        text_at("WHEEL:", (u64)(x + 10), (u64)(y + 78), 2u, CLR_TEXT_DIM, body_color);
        p = 0;
        if (vals[3] > 0) { line[p++] = '+'; }
        else if (vals[3] < 0) { line[p++] = '-'; }
        v = vals[3] < 0 ? -vals[3] : vals[3]; ni = 0;
        if (!v) line[p++] = '0';
        while (v && ni < 10) { num[ni++] = (char)('0' + v % 10); v /= 10; }
        while (ni--) line[p++] = num[ni];
        line[p] = '\0';
        text_at(line, (u64)(x + 82), (u64)(y + 78), 2u, CLR_TEXT, body_color);
    } else if (identity == NABOUT) {
        if (desktop_have_uname) {
            text_at(desktop_uname.sysname, (u64)(x + 10), (u64)(y + 36), 2u, CLR_TEXT, WTB[i]);
            text_at(desktop_uname.release, (u64)(x + 10), (u64)(y + 58), 2u, CLR_TEXT_DIM, body_color);
            text_at(desktop_uname.machine, (u64)(x + 10), (u64)(y + 78), 2u, CLR_TEXT_DIM, body_color);
        } else {
            text_at("uname unavailable", (u64)(x + 10), (u64)(y + 44), 2u, 0xcc8888u, body_color);
        }
        text_at("Framebuffer desktop", (u64)(x + 10), (u64)(y + 112), 2u, CLR_TEXT_DIM, body_color);
    } else if (identity == NFILES) {
        text_at("Programs in /bin", (u64)(x + 10), (u64)(y + 34), 2u, CLR_TEXT, body_color);
        if (!bin_entry_count)
            text_at("Could not read /bin", (u64)(x + 10), (u64)(y + 54), 2u, 0xcc8888u, body_color);
        for (int row = 0; row < (int)BIN_VISIBLE; row++) {
            int e = bin_scroll + row;
            if (e >= bin_entry_count) break;
            u64 row_y = (u64)(y + 54 + row * 15);
            if (row_y + 14u > (u64)(y + h - 2)) break;
            int hover = mx >= x + 6 && mx < x + w - 6 &&
                        my >= (long)row_y && my < (long)row_y + 14;
            u32 row_bg = hover ? ((mb & 1) ? 0x2c8f8au : 0x25434fu) : body_color;
            if (hover)
                solid((u64)(x + 6), row_y, (u64)(w - 12), 14, row_bg);
            text_at_width(bin_entries[e], (u64)(x + 12), row_y + 2, 2u,
                          hover ? 0xffffffu : CLR_TEXT_DIM, row_bg,
                          (u64)(w - 24));
        }
    }
    clip_enabled = 0;
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
    /* Taskbar buttons reserve a fixed clock zone and divide the rest among
     * the windows that are actually open. */
    {
        u64 bx = 4;
        u64 slot = taskbar_slot_width(pw);
        for (i = 0; (u64)i < MAX_W; i++) {
            if (!WOPEN[i]) continue;
            u64 bw = slot > 4u ? slot - 4u : slot;
            int active = (int)i == focused && !WMIN[i];
            u32 bg = active ? CLR_ACTIVE :
                     (WMIN[i] ? CLR_TASK_MIN : CLR_TASK_OPEN);
            solid(bx, ph - TASKBAR_H + 3, bw, TASKBAR_H - 6, bg);
            hline(bx, ph - TASKBAR_H + 3, bw,
                  active ? 0xffffffu : CLR_TASK_EDGE);
            text_at_width(WTITLE[i], bx + 4, ph - TASKBAR_H + 7,
                          slot >= 54u ? 2u : 1u,
                          active ? CLR_PANEL :
                          (WMIN[i] ? CLR_TEXT_DIM : CLR_TEXT), bg,
                          bw > 8u ? bw - 8u : 0u);
            bx += slot;
        }
    }
    /* Wall clock and date come from the live CMOS time syscall. */
    {
        struct nsh_time t;
        char clock_text[16];
        char *out = clock_text;
        u64 clock_x = pw > TASK_CLOCK_W ? pw - TASK_CLOCK_W : 0;
        if (sys_gettime(&t) == 0) {
            out = append_two_digits(out, t.hour);
            *out++ = ':';
            out = append_two_digits(out, t.min);
            *out++ = ' ';
            out = append_two_digits(out, t.mon);
            *out++ = '/';
            out = append_two_digits(out, t.day);
            *out = '\0';
        } else {
            nstrcpy(clock_text, "--:-- --/--");
        }
        solid(clock_x, ph - TASKBAR_H + 2,
              pw - clock_x, TASKBAR_H - 3, CLR_TASKBAR);
        text_at(clock_text, clock_x + 8, ph - TASKBAR_H + 7, 1u,
                CLR_TEXT, CLR_TASKBAR);
    }
    /* Panel: uname supplies the release; sysinfo supplies live uptime. */
    solid(0, 0, pw, PANEL_H, CLR_PANEL);
    hline(0, PANEL_H - 1, pw, 0x354252u);
    solid(8, 6, 15, 15, CLR_ACCENT);
    text_at("N", 11, 10, 2u, 0x101522u, CLR_ACCENT);
    text_at_width(panel_label, 29, 8, 2u, 0xffffffu, CLR_PANEL,
                  pw > 110u ? pw / 2u - 40u : pw - 38u);
    {
        char uptime_text[32];
        char *out = uptime_text;
        out = append_number(out, uptime / 3600u);
        *out++ = ':';
        out = append_two_digits(out, (uptime / 60u) % 60u);
        *out++ = ':';
        out = append_two_digits(out, uptime % 60u);
        *out = '\0';
        u64 text_w = nstrlen(uptime_text) * 4u;
        u64 x = pw > text_w + 10u ? pw - text_w - 10u : 0;
        text_at(uptime_text, x, 10, 1u, CLR_TEXT_DIM, CLR_PANEL);
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
    u64 mseq = ~0ull; /* accept the driver's initial position on first poll */
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
        struct nsh_uname u;
        nputs("desktop: no framebuffer - text mode\n");
        if (sys_uname(&u) == 0) {
            nputs(u.sysname);
            nputs(" ");
            nputs(u.release);
            nputs(" - text desktop\n");
        }
        nputs("Type 'help' for commands. Desktop features require Limine boot.\n");
        nputs("Available: ls cat echo hello newpkg vid desktop nano about\n");
        nputs("Framebuffer support is required; returning to shell.\n");
        return 1;
    }
    if (!fi.width || !fi.height || fi.width < 300 || fi.height < 100) {
        nputs("desktop: framebuffer geometry is too small\n");
        return 1;
    }
    if (fi.width > DESK_MAX_W || fi.height > DESK_MAX_H) {
        nputs("desktop: framebuffer exceeds the 2048x1200 canvas limit\n");
        return 1;
    }
    u64 pw = fi.width;
    u64 ph = fi.height;
    screen_w = pw; screen_h = ph;
    {
        static const long base_w[MAX_W] = {350, 330, 350, 320};
        static const long base_h[MAX_W] = {180, 190, 160, 180};
        long margin = pw > 64u ? 18 : 0;
        long left = margin;
        long usable_w = (long)pw - 2 * margin;
        long top = (long)PANEL_H + 12;
        long bottom = (long)ph - (long)TASKBAR_H - 8;
        long usable_h = bottom - top;
        int two_columns = usable_w >= 400;
        int two_rows = two_columns && usable_h >= 2 * ((long)PANEL_H + 20) + 16;
        long column_w = two_columns ? (usable_w - 16) / 2 : usable_w;
        long row_h = two_rows ? (usable_h - 16) / 2 : usable_h;
        long row_offset = two_rows ? row_h + 16 : 20;
        for (int wi = 0; wi < (int)MAX_W; wi++) {
            long max_x, max_y;
            WW[wi] = base_w[wi] < column_w ? base_w[wi] : column_w;
            WH[wi] = base_h[wi] < row_h ? base_h[wi] : row_h;
            if (WW[wi] < 40) WW[wi] = 40;
            if (WH[wi] < 1) WH[wi] = 1;
            if (two_columns) {
                WX[wi] = left + (wi % 2) * (column_w + 16);
                WY[wi] = top + (two_rows ? (wi / 2) * row_offset :
                                (wi / 2) * 20);
            } else {
                WX[wi] = left + wi * 18;
                WY[wi] = top + wi * 18;
            }
            max_x = (long)pw - WW[wi];
            max_y = (long)ph - (long)TASKBAR_H - WH[wi];
            if (WX[wi] > max_x) WX[wi] = max_x;
            if (WY[wi] > max_y) WY[wi] = max_y;
            if (WX[wi] < 0) WX[wi] = 0;
            if (WY[wi] < (long)PANEL_H) WY[wi] = PANEL_H;
        }
    }
    load_bin_entries();
    if (sys_uname(&desktop_uname) == 0) {
        desktop_have_uname = 1;
        nstrcpy(panel_label, desktop_uname.sysname);
        nstrcat(panel_label, " ");
        nstrcat(panel_label, desktop_uname.release);
    }
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
        int nb, nw, changed = 0, i, on_taskbar = 0, hit_window = -1;
        int new_packet = 0;

        if (mouse_poll(&nx, &ny, &nb, &nw, &mseq) == 0) {
            new_packet = 1;
            if (nx != mx || ny != my || nb != mb || nw != mw)
                changed = 1;
            mx = nx; my = ny; mb = nb; mw = nw;
        }
        /* Pixel coordinates straight from the driver: no 80x25 scaling. */
        px = mx;
        py = my;

        if (new_packet && mw != 0) {
            for (i = 0; i < (int)MAX_W; i++) {
                if (WID[i] != NFILES || !WOPEN[i] || WMIN[i]) continue;
                if (px >= WX[i] && px < WX[i] + WW[i] &&
                    py >= WY[i] + PANEL_H && py < WY[i] + WH[i]) {
                    int max_scroll = bin_entry_count - (int)BIN_VISIBLE;
                    if (max_scroll < 0) max_scroll = 0;
                    bin_scroll += mw > 0 ? -1 : 1;
                    if (bin_scroll < 0) bin_scroll = 0;
                    if (bin_scroll > max_scroll) bin_scroll = max_scroll;
                    changed = 1;
                    break;
                }
            }
        }

        if ((mb & 1) && !(pmb & 1)) {
            if (px >= 4 && py >= (long)(ph - TASKBAR_H) && py < (long)ph) {
                u64 bx = 4;
                u64 slot = taskbar_slot_width(pw);
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (!WOPEN[i]) continue;
                    u64 bw = slot > 4u ? slot - 4u : slot;
                    if ((u64)px >= bx && (u64)px < bx + bw) {
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
                    bx += slot;
                }
                on_taskbar = 1;
            }
            if (!on_taskbar) {
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (WOPEN[i] && !WMIN[i] &&
                        px >= WX[i] && px < WX[i] + WW[i] &&
                        py >= WY[i] && py < WY[i] + WH[i]) {
                        hit_window = i;
                        break;
                    }
                }
                for (i = 0; (u64)i < MAX_W; i++) {
                    if (i != hit_window || !WOPEN[i] || WMIN[i]) continue;
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
                    if (i != hit_window || !WOPEN[i] || WMIN[i]) continue;
                    if (px >= WX[i] && px < WX[i] + WW[i] &&
                        py >= WY[i] && py < WY[i] + PANEL_H) {
                        raise_window(i);
                        focused = 0;
                        drag = 0;
                        grab_ox = px - WX[0]; grab_oy = py - WY[0];
                        changed = 1;
                        break;
                    }
                    if (px >= WX[i] && px < WX[i] + WW[i] &&
                        py >= WY[i] + PANEL_H && py < WY[i] + WH[i]) {
                        int entry = -1;
                        if (WID[i] == NFILES && px >= WX[i] + 6 &&
                            px < WX[i] + WW[i] - 6 && py >= WY[i] + 54 &&
                            py < WY[i] + 54 + (long)BIN_VISIBLE * 15 &&
                            py < WY[i] + WH[i] - 2)
                            entry = bin_scroll +
                                (int)(py - (WY[i] + 54)) / 15;
                        raise_window(i);
                        focused = 0;
                        drag = -1;
                        changed = 1;
                        on_taskbar = 1;
                        if (entry >= 0 && entry < bin_entry_count)
                            launch_bin_entry(entry);
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
            u64 old_used = mem_used;
            u64 old_total = mem_total;
            mem_total = si.total_frames * 4096u / (1024u * 1024u);
            u64 mem_free = si.free_frames * 4096u / (1024u * 1024u);
            mem_used = mem_free < mem_total ? mem_total - mem_free : 0;
            uptime = si.uptime_sec;
            if (uptime != old_uptime || mem_used != old_used ||
                mem_total != old_total) changed = 1;
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
