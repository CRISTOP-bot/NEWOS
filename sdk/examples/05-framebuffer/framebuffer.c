/* 05-framebuffer: direct framebuffer drawing.
 *
 * A real GUI app claims the panel with SYS_CONSOLE_GUI so the kernel's text
 * console stops painting over it, blits XRGB8888 pixels with SYS_FBWRITE,
 * then releases it. Boot via Limine (gui=1) to get a panel; PVH boots have
 * none and the example exits cleanly.
 *
 * Note: the framebuffer is an uncacheable window with no double buffer, so a
 * partial repaint can tear. This is expected on NEWOS.
 */
#include <stdio.h>
#include <newos/nsh.h>

#define W 64
#define H 64

static unsigned int pixels[W * H];

int main(void)
{
    struct nsh_fbinfo fi;
    if (sys_fbinfo(&fi) != 0 || !fi.present || !fi.width || !fi.height) {
        puts("no framebuffer in this boot (use Limine); nothing to draw.");
        return 0;
    }

    printf("panel: %llux%llu, pitch %llu, %u bpp\n",
           fi.width, fi.height, fi.pitch, fi.bpp);
    if (fi.bpp != 32) {
        puts("this example needs a 32-bpp panel.");
        return 1;
    }

    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
            pixels[y * W + x] = 0xff000000u | (x * 4u << 16) |
                                (y * 4u << 8) | 0x40u;

    if (sys_console_gui(1) != 0) {
        puts("another process already owns the panel.");
        return 1;
    }

    struct nsh_fbwrite rq = {0};
    rq.x = (fi.width - W) / 2;
    rq.y = (fi.height - H) / 2;
    rq.w = W;
    rq.h = H;
    rq.pixels = (nsh_u64)(unsigned long)pixels;
    rq.pixlen = sizeof pixels;
    long ret = sys_fbwrite(&rq);

    sys_sleep(1500);            /* keep the square visible for a moment */
    sys_console_gui(0);         /* teardown also releases it automatically */

    printf("fbwrite: %ld\n", ret);
    return ret < 0;
}
