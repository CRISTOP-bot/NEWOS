/* NEWOS userspace hello.
 *
 * Freestanding ring-3 program entered through the ELF entry point directly
 * (no libc, no crt). All I/O goes through the int $0x80 syscall ABI.
 */

typedef unsigned long   u64;
typedef unsigned int    u32;
typedef unsigned long   size_t;

static int sys_write(int fd, const void *buf, u64 len)
{
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(1), "D"((long)fd), "S"(buf), "d"(len)
                         : "memory");
    return (int)ret;
}

static int sys_getpid(void)
{
    long ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(3));
    return (int)ret;
}

static void sys_exit(int code)
{
    __asm__ __volatile__("int $0x80"
                         :
                         : "a"(2), "D"(code));
    for (;;)
        ;
}

static void putdec(int v)
{
    char buf[16];
    int i = sizeof(buf);
    if (v == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (v > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    sys_write(1, &buf[i], sizeof(buf) - (size_t)i);
}

int hello_main(void)
{
    sys_write(1, "Hello from ring 3!\n", 19);
    sys_write(1, "ResidentPID: ", 13);
    putdec(sys_getpid());
    sys_write(1, "\n", 1);
    sys_exit(0);
    return 0;               /* unreachable */
}

void _start(void)
{
    hello_main();
    sys_exit(0);
}