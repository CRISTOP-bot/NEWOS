#include "../include/sys/ioctl.h"
#include "../include/errno.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

struct fbinfo_min {
    unsigned long present;
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned int bpp;
    unsigned int _pad;
};

int ioctl(int fd, unsigned long req, ...)
{
    if (req == TIOCGWINSZ) {
        va_list ap;
        struct winsize *ws;
        struct fbinfo_min fi;
        va_start(ap, req);
        ws = va_arg(ap, struct winsize *);
        va_end(ap);
        if (!ws) {
            errno = EINVAL;
            return -1;
        }
        if (libc_sys1(SYS_FBINFO, (long)&fi) == 0 && fi.present) {
            ws->ws_col = (unsigned short)(fi.width / 8);
            ws->ws_row = (unsigned short)(fi.height / 8);
            ws->ws_xpixel = (unsigned short)fi.width;
            ws->ws_ypixel = (unsigned short)fi.height;
            if (!ws->ws_col)
                ws->ws_col = 80;
            if (!ws->ws_row)
                ws->ws_row = 25;
            return 0;
        }
        ws->ws_col = 80;
        ws->ws_row = 25;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }
    (void)fd;
    errno = ENOTTY;
    return -1;
}
