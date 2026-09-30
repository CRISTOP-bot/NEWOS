#include "../../lib/nshlib.h"

/* vid: procedural animation player on the Limine framebuffer.
 *
 * There is no video codec in the tree (and no ffmpeg port: that needs a
 * userspace libc, heap, threads and FPU codegen), so this program plays
 * a computed animation instead: a bouncing ball over an integer plasma,
 * 60 frames at ~15 fps. It exercises the exact path a future codec
 * would use: decode frame -> FBWRITE blit -> pace with SLEEP.
 * Usage: vid
 */

#define VID_MAX_W 1024u
#define VID_MAX_H 768u
#define VID_FRAMES 60u
#define VID_MS 66u

static u32 frame[VID_MAX_W * VID_MAX_H];

static u32 bg_color(u64 x, u64 y, u64 t)
{
    u64 r = (x * 3u + t * 5u) & 0xFFu;
    u64 g = (y * 5u + t * 3u) & 0xFFu;
    u64 b = ((x ^ y) + t * 7u) & 0xFFu;
    return (u32)((r << 16) | (g << 8) | b);
}

int main(int argc, char **argv)
{
    struct nsh_fbinfo fi;
    u64 vw, vh, x0, y0, f;
    long bx, by, vx, vy;
    long r;

    (void)argc;
    (void)argv;
    if (sys_fbinfo(&fi) != 0 || !fi.present) {
        nputs("vid: no framebuffer (boot the Limine ISO for graphics)\n");
        return 1;
    }
    vw = (fi.width < VID_MAX_W) ? fi.width : VID_MAX_W;
    vh = (fi.height < VID_MAX_H) ? fi.height : VID_MAX_H;
    x0 = (fi.width > vw) ? (fi.width - vw) / 2u : 0;
    y0 = (fi.height > vh) ? (fi.height - vh) / 2u : 0;

    r = (long)((vw < vh ? vw : vh) / 12);
    if (r < 8)
        r = 8;
    bx = (long)(vw / 3);
    by = (long)(vh / 3);
    vx = 7;
    vy = 5;

    for (f = 0; f < VID_FRAMES; f++) {
        u64 y;
        bx += vx;
        by += vy;
        if (bx - r < 0 || bx + r >= (long)vw) {
            vx = -vx;
            bx += 2 * vx;
        }
        if (by - r < 0 || by + r >= (long)vh) {
            vy = -vy;
            by += 2 * vy;
        }
        for (y = 0; y < vh; y++) {
            u64 x;
            for (x = 0; x < vw; x++) {
                long dx = (long)x - bx, dy = (long)y - by;
                if (dx * dx + dy * dy < r * r)
                    frame[y * VID_MAX_W + x] = 0xFFFFFFu;
                else
                    frame[y * VID_MAX_W + x] = bg_color(x, y, f);
            }
        }
        {
            /* Row by row: frame rows are pitched at VID_MAX_W, so a
             * multi-row blit would read across row boundaries. */
            u64 yy;
            for (yy = 0; yy < vh; yy++) {
                struct nsh_fbwrite rq;
                rq.x = x0;
                rq.y = y0 + yy;
                rq.w = vw;
                rq.h = 1;
                rq.pixels = (u64)&frame[yy * VID_MAX_W];
                rq.pixlen = vw * 4u;
                if (sys_fbwrite(&rq) != 0) {
                    nputs("vid: blit failed\n");
                    return 1;
                }
            }
        }
        sys_sleep(VID_MS);
    }
    nputs("vid: done\n");
    return 0;
}
