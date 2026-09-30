#include "../include/signal.h"

sighandler_t signal(int sig, sighandler_t fn)
{
    (void)sig;
    (void)fn;
    return 0;   /* accepted and ignored: no delivery exists yet */
}

int raise(int sig)
{
    (void)sig;
    return -1;
}
