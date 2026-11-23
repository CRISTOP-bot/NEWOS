/* 06-processes: spawn a child and collect its exit code.
 *
 * fork()/execve() are not implemented on NEWOS. The native primitive is
 * SYS_SPAWN2/3: load another ELF from the VFS with an explicit argv and
 * optional fd redirection. SYS_WAITPID returns the child's exit code.
 *
 * Run from a booted image that has /bin/hello built in.
 */
#include <stdio.h>
#include <newos/nsh.h>

int main(void)
{
    char *argv[] = { "/bin/hello", (char *)0 };
    long pid = sys_spawn3("/bin/hello", argv, 1, 0, 1);
    if (pid < 0) {
        printf("spawn failed (%ld)\n", pid);
        return 1;
    }
    printf("child pid %ld\n", pid);
    long code = sys_waitpid((int)pid);
    printf("child exit code %ld\n", code);
    return 0;
}
