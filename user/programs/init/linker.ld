/* NEWOS userspace init shell linker script: a tiny static ELF64 at 0x400000. */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
    }

    /* Throw away everything else. */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}