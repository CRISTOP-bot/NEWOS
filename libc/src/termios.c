#include "../include/termios.h"

int tcgetattr(int fd, struct termios *t)
{
    (void)fd;
    if (!t)
        return -1;
    t->c_iflag = 0;
    t->c_oflag = 0;
    t->c_cflag = 0;
    t->c_lflag = 0;
    {
        int i;
        for (i = 0; i < 16; i++)
            t->c_cc[i] = 0;
    }
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t)
{
    (void)fd;
    (void)action;
    (void)t;
    return 0;   /* console is already raw */
}
