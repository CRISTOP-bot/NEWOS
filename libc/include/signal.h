#ifndef NEWOS_LIBC_SIGNAL_H
#define NEWOS_LIBC_SIGNAL_H

/* Signals do not exist yet (no async delivery, no handlers). The
 * declarations exist so ports compile; signal() reports failure and
 * raise() does nothing. */

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGTERM 15
#define SIGWINCH 28

typedef void (*sighandler_t)(int);

sighandler_t signal(int sig, sighandler_t fn);
int raise(int sig);

#endif
