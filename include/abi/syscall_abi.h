#ifndef UAPI_SYSCALL_H
#define UAPI_SYSCALL_H

/* User/kernel shared syscall ABI.
 *
 * System calls are triggered with `int $0x80` (trap gate, DPL 3):
 *     rax = syscall number
 *     rdi, rsi, rdx, r10, r8, r9 = arguments (SysV order)
 *     rax = return value (>= 0 on success, negative errno-ish on error)
 *
 * The only registers preserved guarantees: all GPRs except rax are restored
 * (rcx and r11 are clobbered by the instruction, matching the SysV ABI).
 */

/* Syscall numbers follow Linux x86_64 (asm/unistd_64.h) so an unmodified
 * Linux-format static binary (musl toolchain output) can run here. Calls
 * with no Linux counterpart live in the NEWOS vendor block 450+. */

#define SYS_READ     0
#define SYS_WRITE    1
#define SYS_OPEN     2
#define SYS_CLOSE    3
#define SYS_STAT     4
#define SYS_FSTAT    5
#define SYS_LSTAT    6
#define SYS_POLL     7
#define SYS_LSEEK    8
#define SYS_MMAP     9
#define SYS_MPROTECT 10
#define SYS_MUNMAP   11
#define SYS_BRK      12
#define SYS_RT_SIGACTION   13
#define SYS_RT_SIGPROCMASK 14
#define SYS_IOCTL    16
#define SYS_READV    19
#define SYS_WRITEV   20
#define SYS_ACCESS   21
#define SYS_PIPE     22
#define SYS_SCHED_YIELD 24
#define SYS_MADVISE  28
#define SYS_DUP      32
#define SYS_DUP2     33
#define SYS_SLEEP    35   /* Linux nanosleep: rdi = struct nsh_timespec * */
#define SYS_GETPID   39
#define SYS_FORK     57
#define SYS_EXECVE   59
#define SYS_EXIT     60
#define SYS_WAITPID  61   /* Linux wait4: rdi=pid, rsi=int *status */
#define SYS_KILL     62
#define SYS_UNAME    63
#define SYS_FCNTL    72
#define SYS_TRUNCATE 76
#define SYS_FTRUNCATE 77
#define SYS_GETCWD   79
#define SYS_CHDIR    80
#define SYS_RENAME   82
#define SYS_MKDIR    83
#define SYS_RMDIR    84
#define SYS_UNLINK   87
#define SYS_READLINK 89
#define SYS_UMASK    95
#define SYS_GETTIMEOFDAY 96
#define SYS_TIMES    100
#define SYS_GETUID   102
#define SYS_GETGID   104
#define SYS_GETEUID  107
#define SYS_GETEGID  108
#define SYS_GETPPID  110
#define SYS_GETPGRP  111
#define SYS_SETSID   112
#define SYS_SIGALTSTACK 131
#define SYS_ARCH_PRCTL 158
#define SYS_GETDENTS64 217
#define SYS_SET_TID_ADDRESS 218
#define SYS_CLOCK_GETTIME 228
#define SYS_EXIT_GROUP 231
#define SYS_NEWFSTATAT 262
#define SYS_GETRANDOM 318

/* NEWOS vendor block. */
#define SYS_SPAWN      450
#define SYS_SPAWN2     451
#define SYS_SPAWN3     452
#define SYS_FBINFO     453
#define SYS_FBWRITE    454
#define SYS_MOUSE_GET  455
#define SYS_PS         456
#define SYS_READDIR    457   /* legacy nsh_dirent form, path-based */
#define SYS_GETTIME    458   /* CMOS nsh_time form */
#define SYS_SYSINFO    459
#define SYS_SOCKET     460
#define SYS_BIND       461
#define SYS_CONNECT    462
#define SYS_ACCEPT     463
#define SYS_LISTEN     464
#define SYS_SENDTO     465
#define SYS_RECVFROM   466
#define SYS_GETSOCKNAME 467
#define SYS_GETPEERNAME 468
#define SYS_SHUTDOWN   469
#define SYS_SEND       470   /* stream send, no peer address */
#define SYS_RECV       471   /* stream recv, no peer address */

/* SEEK_* for SYS_LSEEK (rdi=fd, rsi=offset, rdx=whence; returns new pos). */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Conventional file descriptors. */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Negative return values are errors (simple -1 for now). */
#define SYSCALL_RET_ERROR (-1)

/* Shared record types. Self-contained fixed-width types (no core headers):
 * the kernel converts to/from its own u8..u64 when filling them. */
typedef unsigned char      nsh_u8;
typedef unsigned short     nsh_u16;
typedef unsigned int       nsh_u32;
typedef int                nsh_s32;
typedef unsigned long      nsh_u64;
typedef long               nsh_s64;

#define NSH_MAX_ARGS    16
#define NSH_MAX_ARGLEN  128
#define NSH_MAX_PATH    256
#define NSH_NAME_LEN    64

/* ---- Linux-compatible open/fcntl flags ------------------------------ */
#define NSH_O_RDONLY   0
#define NSH_O_WRONLY   1
#define NSH_O_RDWR     2
#define NSH_O_ACCMODE  3
#define NSH_O_CREAT    00000100
#define NSH_O_EXCL     00000200
#define NSH_O_NOCTTY   00000400
#define NSH_O_TRUNC    00001000
#define NSH_O_APPEND   00002000
#define NSH_O_NONBLOCK 00004000
#define NSH_O_CLOEXEC  002000000

#define NSH_F_DUPFD         0
#define NSH_F_GETFD         1
#define NSH_F_SETFD         2
#define NSH_F_GETFL         3
#define NSH_F_SETFL         4
#define NSH_F_GETOWN        9
#define NSH_F_SETOWN        6
#define NSH_FD_CLOEXEC      1

/* ---- mmap -------------------------------------------------------------- */
#define NSH_PROT_NONE  0
#define NSH_PROT_READ  1
#define NSH_PROT_WRITE 2
#define NSH_PROT_EXEC  4
#define NSH_MAP_SHARED    1
#define NSH_MAP_PRIVATE   2
#define NSH_MAP_FIXED     0x10
#define NSH_MAP_ANONYMOUS 0x20

/* ---- ioctl console requests (Linux x86_64 values) ---------------------- */
#define NSH_TCGETS     0x5401
#define NSH_TCSETS     0x5402
#define NSH_TCSETSW    0x5403
#define NSH_TCSETSF    0x5404
#define NSH_TIOCGWINSZ 0x5413
#define NSH_TIOCSWINSZ 0x5414
#define NSH_FIONREAD   0x541B

/* ---- arch_prctl codes --------------------------------------------------- */
#define NSH_ARCH_SET_GS 0x1001
#define NSH_ARCH_SET_FS 0x1002
#define NSH_ARCH_GET_FS 0x1003
#define NSH_ARCH_GET_GS 0x1004

/* ---- auxiliary vector tags (AT_*) -------------------------------------- */
#define NSH_AT_NULL     0
#define NSH_AT_PHDR     3
#define NSH_AT_PHENT    4
#define NSH_AT_PHNUM    5
#define NSH_AT_PAGESZ   6
#define NSH_AT_BASE     7
#define NSH_AT_FLAGS    8
#define NSH_AT_ENTRY    9
#define NSH_AT_UID      11
#define NSH_AT_GID      12
#define NSH_AT_EUID     13
#define NSH_AT_EGID     14
#define NSH_AT_HWCAP    16
#define NSH_AT_SECURE   23
#define NSH_AT_RANDOM   25
#define NSH_AT_EXECFN   31

/* ---- getdents64 file types --------------------------------------------- */
#define NSH_DT_UNKNOWN 0
#define NSH_DT_FIFO    1
#define NSH_DT_CHR     2
#define NSH_DT_DIR     4
#define NSH_DT_REG     8

/* SYS_READDIR record: one per directory child. */
struct nsh_dirent {
    char   name[NSH_NAME_LEN];
    nsh_u32 mode;                 /* VFS_MODE_* bits */
    nsh_u64 size;
};

/* SYS_PS record: one per runnable/zombie process. */
struct nsh_proc {
    nsh_s64 pid;
    nsh_s64 ppid;
    nsh_s64 state;                /* PROCESS_STATE_* */
    char   name[32];
};

/* SYS_UNAME record: Linux struct utsname layout. */
struct nsh_uname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* SYS_GETTIME record: real CMOS clock, 24h. */
struct nsh_time {
    nsh_u16 year;
    nsh_u8  mon;
    nsh_u8  day;
    nsh_u8  hour;
    nsh_u8  min;
    nsh_u8  sec;
    nsh_u8  _pad;
};

/* SYS_SYSINFO record: memory + uptime. */
struct nsh_sysinfo {
    nsh_u64 total_frames;
    nsh_u64 free_frames;
    nsh_u64 uptime_sec;
    nsh_u64 hz;
};

/* SYS_FBINFO record: panel geometry (all zero when no framebuffer). */
struct nsh_fbinfo {
    nsh_u64 present;
    nsh_u64 width;
    nsh_u64 height;
    nsh_u64 pitch;
    nsh_u32 bpp;
    nsh_u32 _pad;
};

/* SYS_FBWRITE request: blit w*h XRGB8888 pixels from the user buffer
 * `pixels` (exactly w*h*4 bytes) at panel offset (x, y). */
struct nsh_fbwrite {
    nsh_u64 x;
    nsh_u64 y;
    nsh_u64 w;
    nsh_u64 h;
    nsh_u64 pixels;     /* user pointer */
    nsh_u64 pixlen;     /* bytes available at pixels */
};

/* SYS_MOUSE_GET record: latest PS/2 pointer state in device pixels.
 * `seq` increments per hardware packet; `wheel` is the signed delta of
 * the packet that produced the current state (0 when none). */
struct nsh_mouse {
    nsh_s64 x;
    nsh_s64 y;
    nsh_u64 seq;
    nsh_u32 buttons;
    nsh_s32 wheel;
};

/* ---- Linux-layout records used by real binaries ------------------------ */

/* SYS_STAT / SYS_FSTAT / SYS_LSTAT / SYS_NEWFSTATAT record: identical to
 * the x86_64 struct stat of musl and glibc. */
struct nsh_stat {
    nsh_u64 st_dev;
    nsh_u64 st_ino;
    nsh_u64 st_nlink;
    nsh_u32 st_mode;
    nsh_u32 st_uid;
    nsh_u32 st_gid;
    nsh_u32 __st_pad0;
    nsh_u64 st_rdev;
    nsh_s64 st_size;
    nsh_s64 st_blksize;
    nsh_s64 st_blocks;
    nsh_s64 st_atime;
    nsh_s64 st_atime_nsec;
    nsh_s64 st_mtime;
    nsh_s64 st_mtime_nsec;
    nsh_s64 st_ctime;
    nsh_s64 st_ctime_nsec;
    nsh_u64 __st_gen;
    nsh_u64 __st_spare[3];
};

/* SYS_GETDENTS64 record header: variable-length (header + name + NUL,
 * padded to 8). Only the fixed part is a complete type here. */
struct nsh_dirent64 {
    nsh_u64 d_ino;
    nsh_u64 d_off;
    nsh_u16 d_reclen;
    nsh_u8  d_type;
    char    d_name[1];      /* flexible; reclen covers the real bytes */
};

struct nsh_timespec {
    nsh_s64 tv_sec;
    nsh_s64 tv_nsec;
};

struct nsh_timeval {
    nsh_s64 tv_sec;
    nsh_s64 tv_usec;
};

/* SYS_TIMES record (struct tms). */
struct nsh_tms {
    nsh_u64 tms_utime;
    nsh_u64 tms_stime;
    nsh_u64 tms_cutime;
    nsh_u64 tms_cstime;
};

/* SYS_IOCTL(TCGETS/TCSETS) record: Linux struct termios. */
struct nsh_termios {
    nsh_u32 c_iflag;
    nsh_u32 c_oflag;
    nsh_u32 c_cflag;
    nsh_u32 c_lflag;
    nsh_u8  c_cc[19];
    nsh_u8  __c_pad;
    nsh_u32 c_ispeed;
    nsh_u32 c_ospeed;
};

/* SYS_IOCTL(TIOCGWINSZ) record: Linux struct winsize. */
struct nsh_winsize {
    nsh_u16 ws_row;
    nsh_u16 ws_col;
    nsh_u16 ws_xpixel;
    nsh_u16 ws_ypixel;
};

#endif
