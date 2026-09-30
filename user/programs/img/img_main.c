#include "../../lib/nshlib.h"

/* img: show a picture on the Limine framebuffer.
 *
 * Supported formats (decoded natively, no external libraries):
 *   BMP  - uncompressed 24/32-bit Windows bitmaps
 *   PPM  - binary P6 portable pixmaps ("P6 <w> <h> <255>\n" + RGB bytes)
 * Usage: img <file>      (any key returns to the shell)
 *
 * JPEG/PNG are refused with a clear message: they need an inflate/IDCT
 * codec plus a userspace heap and FPU codegen, none of which exists yet
 * (userland builds -mgeneral-regs-only, nshlib has no malloc). The
 * documented path is vendoring stb_image once those land; the BMP/PPM
 * path here already proves file -> pixels -> panel end to end.
 */

#define IMG_MAX_FILE (4u * 1024u * 1024u)
#define IMG_MAX_DIM  2048u

static u8 filebuf[IMG_MAX_FILE];
static u32 pixels[IMG_MAX_DIM * 512u];   /* one 512-row decode window */

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u32 rd16(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8);
}

struct bmp_desc {
    u64 off;
    u64 w;
    u64 h;
    u64 rowbytes;
    u32 bpp;
};

/* Validate a BMP header; 1 = 24/32-bit uncompressed, descriptor filled. */
static int bmp_open(const u8 *data, u64 len, struct bmp_desc *d)
{
    u64 ww, hh;
    u32 bpp, comp;

    if (len < 54 || data[0] != 'B' || data[1] != 'M')
        return 0;
    if (rd32(data + 14) != 40)      /* BITMAPINFOHEADER only */
        return 0;
    ww = rd32(data + 18);
    hh = rd32(data + 22);
    if (!ww || !hh || ww > IMG_MAX_DIM || hh > IMG_MAX_DIM)
        return 0;
    if (rd16(data + 26) != 1)
        return 0;
    bpp = rd16(data + 28);
    if (bpp != 24 && bpp != 32)
        return 0;
    comp = rd32(data + 30);
    if (comp != 0)                  /* BI_RGB (uncompressed) only */
        return 0;
    d->off = rd32(data + 10);
    d->w = ww;
    d->h = hh;
    d->bpp = bpp;
    d->rowbytes = ((ww * bpp + 31u) / 32u) * 4u;
    if (d->off + d->rowbytes * hh > len)
        return 0;
    return 1;
}

/* Decode top-origin rows [y0, y1) into the window. */
static void bmp_band(const u8 *data, const struct bmp_desc *d, u64 y0,
                     u64 y1)
{
    u64 y;
    for (y = y0; y < y1; y++) {
        const u8 *src = data + d->off + (d->h - 1 - y) * d->rowbytes;
        u64 x;
        for (x = 0; x < d->w; x++) {
            u8 b = src[0], g = src[1], r = src[2];
            pixels[(y - y0) * IMG_MAX_DIM + x] =
                ((u32)r << 16) | ((u32)g << 8) | b;
            src += d->bpp / 8u;
        }
    }
}

static int is_space(u8 c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

struct ppm_desc {
    u64 w;
    u64 h;
    u64 body;       /* offset of the first raster byte */
};

/* Validate a binary PPM (P6) header; skips comments/whitespace. */
static int ppm_open(const u8 *data, u64 len, struct ppm_desc *d)
{
    u64 p = 0, vals[3] = { 0, 0, 0 };
    int field = 0;

    if (len < 4 || data[0] != 'P' || data[1] != '6')
        return 0;
    p = 2;
    while (field < 3 && p < len) {
        if (data[p] == '#') {
            while (p < len && data[p] != '\n')
                p++;
            continue;
        }
        if (is_space(data[p])) {
            p++;
            continue;
        }
        {
            u64 v = 0;
            if (data[p] < '0' || data[p] > '9')
                return 0;
            while (p < len && data[p] >= '0' && data[p] <= '9') {
                v = v * 10 + (u64)(data[p] - '0');
                p++;
            }
            vals[field++] = v;
        }
    }
    if (field != 3 || vals[2] != 255 || !vals[0] || !vals[1])
        return 0;
    if (vals[0] > IMG_MAX_DIM || vals[1] > IMG_MAX_DIM)
        return 0;
    if (p >= len || !is_space(data[p]))
        return 0;
    p++;
    if (p + vals[0] * vals[1] * 3u > len)
        return 0;
    d->w = vals[0];
    d->h = vals[1];
    d->body = p;
    return 1;
}

/* Decode top-origin rows [y0, y1) into the window. */
static void ppm_band(const u8 *data, const struct ppm_desc *d, u64 y0,
                     u64 y1)
{
    u64 y;
    for (y = y0; y < y1; y++) {
        const u8 *src = data + d->body + y * d->w * 3u;
        u64 x;
        for (x = 0; x < d->w; x++) {
            u8 r = src[0], g = src[1], b = src[2];
            pixels[(y - y0) * IMG_MAX_DIM + x] =
                ((u32)r << 16) | ((u32)g << 8) | b;
            src += 3;
        }
    }
}

static long read_all(int fd, u8 *dst, u64 cap)
{
    u64 got = 0;
    for (;;) {
        long n;
        u64 want = cap - got;
        if (want > 32768u)
            want = 32768u;
        if (!want)
            break;
        n = sys_read(fd, dst + got, want);
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        got += (u64)n;
    }
    return (long)got;
}

int main(int argc, char **argv)
{
    struct nsh_fbinfo fi;
    struct bmp_desc bmp;
    struct ppm_desc ppm;
    int is_bmp = 0;
    long fd, n;
    u64 w, h, x0, y0, y;

    if (argc != 2) {
        nputs("usage: img <file.bmp|file.ppm>\n");
        return 1;
    }
    if (sys_fbinfo(&fi) != 0 || !fi.present) {
        nputs("img: no framebuffer (boot the Limine ISO for graphics)\n");
        return 1;
    }
    fd = sys_open(argv[1], O_READ);
    if (fd < 0) {
        fputf(1, "img: %s: no such file\n", argv[1]);
        return 1;
    }
    n = read_all((int)fd, filebuf, sizeof(filebuf));
    sys_close((int)fd);
    if (n <= 0) {
        nputs("img: empty or unreadable file\n");
        return 1;
    }

    if (n >= 2 && filebuf[0] == 'B' && filebuf[1] == 'M') {
        is_bmp = 1;
        if (!bmp_open(filebuf, (u64)n, &bmp)) {
            fputf(1, "img: %s: cannot decode (need 24/32-bit BMP)\n",
                  argv[1]);
            return 1;
        }
        w = bmp.w;
        h = bmp.h;
    } else if (n >= 2 && filebuf[0] == 'P' && filebuf[1] == '6') {
        if (!ppm_open(filebuf, (u64)n, &ppm)) {
            fputf(1, "img: %s: cannot decode (need P6 PPM)\n", argv[1]);
            return 1;
        }
        w = ppm.w;
        h = ppm.h;
    } else if (n >= 4 && filebuf[0] == 0xFF && filebuf[1] == 0xD8) {
        nputs("img: JPEG needs a userspace codec (no FPU/heap yet)\n");
        return 1;
    } else if (n >= 4 && filebuf[0] == 0x89 && filebuf[1] == 'P') {
        nputs("img: PNG needs inflate + codec (no heap yet)\n");
        return 1;
    } else {
        fputf(1, "img: %s: unsupported format (BMP/PPM only)\n", argv[1]);
        return 1;
    }

    /* Center, clipped to the panel, in 512-row bands. */
    x0 = (w < fi.width) ? (fi.width - w) / 2u : 0;
    y0 = (h < fi.height) ? (fi.height - h) / 2u : 0;
    y = 0;
    while (y < h) {
        u64 band = h - y, r;
        if (y0 + y >= fi.height)
            break;
        if (band > 512u)
            band = 512u;
        if (band > fi.height - (y0 + y))
            band = fi.height - (y0 + y);
        if (is_bmp)
            bmp_band(filebuf, &bmp, y, y + band);
        else
            ppm_band(filebuf, &ppm, y, y + band);
        for (r = 0; r < band; r++) {
            struct nsh_fbwrite rq;
            rq.x = x0;
            rq.y = y0 + y + r;
            rq.w = (w < fi.width) ? w : fi.width;
            rq.h = 1;
            rq.pixels = (u64)&pixels[r * IMG_MAX_DIM];
            rq.pixlen = rq.w * 4u;
            if (sys_fbwrite(&rq) != 0) {
                nputs("img: blit failed\n");
                return 1;
            }
        }
        y += band;
    }

    nputs("img: press any key\n");
    {
        char c;
        sys_read(0, &c, 1);
    }
    return 0;
}
