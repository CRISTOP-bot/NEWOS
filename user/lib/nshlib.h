/* nshlib: shared userland library for NEWOS /bin programs.
 *
 * Freestanding (no libc): thin int $0x80 wrappers plus the small string
 * and output helpers every command needs. Numbers and record layouts come
 * straight from the kernel ABI so the two sides cannot drift apart.
 */

#ifndef NSHLIB_H
#define NSHLIB_H

#include "../../abi/syscall_abi.h"

typedef unsigned long  u64;
typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef long           s64;
typedef unsigned long  size_t;
typedef int            pid_t;

/* open() flags: Linux x86_64 values (the kernel translates them to the
 * VFS read/write/create model). */
#define O_READ   0                    /* O_RDONLY */
#define O_WRITE  1                    /* O_WRONLY */
#define O_RDWR   2
#define O_CREATE 00000100             /* O_CREAT */

/* inode mode bits (mirror of the kernel VFS modes). */
#define MODE_DIR  0040000
#define MODE_REG  0100000
#define MODE_CHAR 0020000

/* --- syscalls ---------------------------------------------------------- */
long sys_read(int fd, void *buf, u64 len);
long sys_write(int fd, const void *buf, u64 len);
long sys_open(const char *path, int flags);
long sys_close(int fd);
long sys_mkdir(const char *path);
long sys_unlink(const char *path);
long sys_rename(const char *oldpath, const char *newpath);
long sys_readdir(const char *path, struct nsh_dirent *buf, u64 bytes);
long sys_chdir(const char *path);
long sys_getcwd(char *buf, u64 size);
long sys_spawn2(const char *path, char **argv, int argc);
long sys_waitpid(pid_t pid);
long sys_getpid(void);
long sys_sleep(u64 ms);
long sys_ps(struct nsh_proc *buf, u64 bytes);
long sys_kill(pid_t pid);
long sys_uname(struct nsh_uname *u);
long sys_gettime(struct nsh_time *t);
long sys_sysinfo(struct nsh_sysinfo *si);
long sys_fbinfo(struct nsh_fbinfo *fi);
long sys_fbwrite(const struct nsh_fbwrite *rq);
long sys_lseek(int fd, long off, int whence);
long sys_mouse_get(struct nsh_mouse *m);
long sys_pipe(int fds[2]);
long sys_spawn3(const char *path, char **argv, int argc, int fdin, int fdout);
void sys_exit(int code);

/* --- strings ----------------------------------------------------------- */
u64   nstrlen(const char *s);
int   nstrcmp(const char *a, const char *b);
int   nstrncmp(const char *a, const char *b, u64 n);
char *nstrcpy(char *d, const char *s);
char *nstrcat(char *d, const char *s);
char *nstrchr(const char *s, char c);
void *nmemcpy(void *d, const void *s, u64 n);
void *nmemset(void *d, int c, u64 n);
int   natoi(const char *s);
u64   natou(const char *s);

/* --- output ------------------------------------------------------------ */
void nputc(char c);
void nputs(const char *s);
void nputsn(const char *s, u64 n);
void putdec(long v);
void putu(u64 v);
void puthex(u64 v);
void put2(u64 v);            /* zero-padded 2 digits (clock fields) */
/* Tiny printf: %s %d %u %x %c %%. */
void putf(const char *fmt, ...);
void fputf(int fd, const char *fmt, ...);

/* Print "prog: msg" + trailing newline to stdout. Returns code. */
int fail(const char *prog, const char *msg);

#endif
