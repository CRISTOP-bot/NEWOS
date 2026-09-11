#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef signed char        s8;
typedef signed short       s16;
typedef signed int         s32;
typedef signed long long   s64;

typedef u64                uintptr_t;
typedef u32                size_t;
typedef s64                ssize_t;
typedef u32                intptr_t;

typedef u64                ulong;
typedef u64                uword_t;
typedef int                bool;

#define true  1
#define false 0
#define NULL  ((void *)0)

typedef u64                pid_t;
typedef u64                uid_t;
typedef u64                gid_t;
typedef u64                mode_t;
typedef s64                off_t;
typedef u64                time_t;
typedef u64                clock_t;

#define __noreturn         __attribute__((noreturn))
#define __packed           __attribute__((packed))

#endif