#ifndef NEWOS_LIBC_SYS_IOCTL_H
#define NEWOS_LIBC_SYS_IOCTL_H

/* Only TIOCGWINSZ is implemented, answered from the live framebuffer
 * geometry (or the 80x25 text mode when booted without graphics).
 * Everything else fails with ENOTTY, like a bare serial line would. */

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413

int ioctl(int fd, unsigned long req, ...);

#endif
