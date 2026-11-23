/* NEWOS SDK: public syscall API.
 *
 * Umbrella for the thin int $0x80 wrappers in libnsh.a. Include this instead
 * of <nshlib.h> in SDK applications: it pulls the ABI records in first and
 * works with both host GCC and the on-board TinyCC (which has no
 * __has_include builtin and would otherwise pick nshlib.h's relative fallback
 * path, which does not exist inside /usr/include).
 *
 * This header is installed as <newos/nsh.h> by `make sdk`. The declarations
 * themselves are owned by user/lib/nshlib.h in NEWOS-userland.
 */
#ifndef NEWOS_SDK_NSH_H
#define NEWOS_SDK_NSH_H

#include <newos/syscall_abi.h>

#ifndef __has_include
#define __has_include(x) 1
#endif
#include <nshlib.h>

#endif /* NEWOS_SDK_NSH_H */
