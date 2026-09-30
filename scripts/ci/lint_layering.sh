#!/usr/bin/env bash
# NEWOS source layering lint.
#
# Enforces the upward-only dependency contract described in the build
# assessment:
#
#   L0 lib/kernel (iru_*)  utility code, may only use CORE types/primitives
#   L1 abi                  public user/kernel interface (self-contained)
#   L2 arch/<plat>          machine-facing code (boot orchestration)      }
#   L3 core                 kernel core: log, oops + init orchestration   }  orchestrators
#   L4 drivers              hardware-facing services
#   L5 mm                   memory management
#   L6 fs                   VFS + backends
#   L7 process              processes/threads/ELF
#   L8 ipc                  inter-process primitives
#   L9 syscall              dispatcher (talks to process + fs + mm)
#   L10 user                userland programs (ABI only)
#   LX libc                 freestanding C library (self-contained + ABI);
#                           userland side only, never imported by the kernel
#
# Rule: a file in domain D may only #include headers whose class is
# either D itself or one of the classes in D's allow list (below). In
# particular, no production domain (drivers/mm/fs/process/ipc/syscall) may
# reach "up" into a closer-to-user domain. arch/ and core/ are permissive
# because boot/init only exist to wire the whole system together.
set -euo pipefail

cd "$(dirname "$0")/../.."

violations=0

# --- classificators ----------------------------------------------------------

# Class of a source or header FILE, from its path.
file_class() {
    case "$1" in
        lib/kernel/*)          echo LIB ;;
        libc/*)                echo LIBC ;;
        abi/*)                 echo ABI ;;
        user/*)                echo USER ;;
        core/*)                echo CORE ;;
        arch/*)                echo ARCH ;;
        drivers/*)             echo DRV ;;
        mm/*)                  echo MM ;;
        fs/*)                  echo FS ;;
        ipc/*)                 echo IPC ;;
        process/*)             echo PROC ;;
        syscall/*)             echo SYSC ;;
        include/core/*)        echo CORE ;;
        include/mm/*)          echo MM ;;
        include/fs/*)          echo FS ;;
        include/drivers/*)     echo DRV ;;
        include/ipc/*)         echo IPC ;;
        include/process/*)     echo PROC ;;
        include/syscall/*)     echo SYSC ;;
        include/*)             echo INC ;;
        *)                     echo UNKNOWN ;;
    esac
}

# Class of an #include target, from the include directive text.
include_class() {
    case "$1" in
        '<abi/'*)       echo ABI ;;
        '<iru_'*)       echo LIB ;;
        '<x86_'*)       echo ARCH ;;
        '<core/'*)      echo CORE ;;
        '<mm/'*)        echo MM ;;
        '<fs/'*)        echo FS ;;
        '<drivers/'*)   echo DRV ;;
        '<ipc/'*)       echo IPC ;;
        '<process/'*)   echo PROC ;;
        '<syscall/'*)   echo SYSC ;;
        '"'*)           echo LOCAL ;;
        "<"*)           echo SYSTEM ;;
        *)              echo UNKNOWN ;;
    esac
}

# Allowed target classes for files of domain $1.
allowed() {
    case "$1" in
        CORE) echo ABI LIB ARCH CORE MM FS DRV IPC PROC SYSC ;;
        ARCH) echo ABI LIB ARCH CORE MM FS DRV IPC PROC SYSC ;;
        DRV)  echo LIB ARCH CORE MM DRV ;;
        MM)   echo LIB ARCH CORE MM ;;
        FS)   echo LIB CORE MM DRV FS ;;
        # PROC may use IPC so process_free can drop pipe references when a
        # process dies; IPC may use MM for pipe storage; SYSC drives pipes.
        PROC) echo LIB ARCH CORE MM FS PROC IPC ;;
        IPC)  echo LIB CORE MM IPC ;;
        SYSC) echo ABI LIB ARCH CORE MM FS PROC IPC SYSC ;;
        LIB)  echo LIB CORE ;;
        LIBC) echo ABI LIBC ;;
        USER) echo ABI LIB ;;
        ABI)  echo ABI CORE ;;
        *)    echo "" ;;
    esac
}

# --- walk --------------------------------------------------------------------

while IFS= read -r f; do
    dom=$(file_class "${f#./}")
    case "$dom" in
        INC|UNKNOWN) continue ;;
    esac

    allowed_classes=$(allowed "$dom")

    while IFS= read -r incl; do
        [ -n "$incl" ] || continue
        tgt=$(include_class "$incl")
        case "$tgt" in
            LOCAL|SYSTEM) continue ;;
            UNKNOWN)
                echo "?? $f: cannot classify '$incl'"
                violations=$((violations + 1))
                continue ;;
        esac

        if [ "$allowed_classes" = "" ] || ! echo " $allowed_classes " | grep -q " $tgt "; then
            echo "LAYERING VIOLATION: $f -> $incl   ($dom must not import $tgt)"
            violations=$((violations + 1))
        fi
    done < <(sed -nE 's/^[[:space:]]*#include[[:space:]]+([<"][^>"]+[>"]).*$/\1/p' "$f")
done < <(find . -type d \( -path './build' -o -path './.git' \
                         -o -path './toolchain/out' -o -path './ports/build' \) \
         -prune -o \
         -type f \( -name '*.c' -o -name '*.h' \) -print | sort)

if [ "$violations" -eq 0 ]; then
    echo "layering: OK (no violations)"
else
    echo "layering: $violations violation(s) found"
    exit 1
fi
