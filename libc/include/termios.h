#ifndef NEWOS_LIBC_TERMIOS_H
#define NEWOS_LIBC_TERMIOS_H

/* The NEWOS console is already a raw byte stream (no kernel echo or
 * line discipline), so raw-mode setup is a no-op that always succeeds.
 * Canonical flag bits are defined for source compatibility. */

#define ECHO 0x08u
#define ICANON 0x02u
#define IEXTEN 0x8000u
#define ISIG 0x01u

/* Input / output / control flag bits (Linux values, for sources that
 * mask them; tcsetattr ignores everything). */
#define BRKINT 0x02u
#define ICRNL 0x0400u
#define INPCK 0x020u
#define ISTRIP 0x040u
#define IXON 0x2000u
#define OPOST 0x01u
#define CSIZE 0x060u
#define CS8 0x060u

#define VMIN 6
#define VTIME 5
#define NCCS 16

#define TCSAFLUSH 2

struct termios {
    unsigned c_iflag;
    unsigned c_oflag;
    unsigned c_cflag;
    unsigned c_lflag;
    unsigned char c_cc[16];
};

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);

#endif
