/* NEWOS x86_64 Limine boot linker script
 *
 * Limine maps every loadable segment at its virtual address and applies a
 * "slide" to executables with loadable segments below 0xffffffff80000000,
 * so this Limine image is linked entirely in the higher half -- none of the
 * low-address trampoline code (.early_text/.early_data, multiboot header,
 * PVH note) is kept. Limine enters the kernel through the entry-point
 * request registered in arch/x86_64/boot/limine.c.
 *
 * The `.limine_requests*` sections must be kept inside a loaded segment so
 * the bootloader can scan the image for the request IDs between the start
 * and end markers.
 */

ENTRY(_limine_entry)

KERNEL_BASE_VA = 0xffffffff80000000;

PHDRS
{
    high_rx PT_LOAD FLAGS(5);
    high_rw PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = KERNEL_BASE_VA;

    .text : {
        _text_start = .;
        *(.text .text.*)
        *(.init .init.*)
        *(.fini)
        *(.limine_entry .limine_entry.*)
        . = ALIGN(4K);
        _text_end = .;
    } :high_rx

    .rodata : {
        _rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4K);
        _rodata_end = .;
    } :high_rx

    .data : {
        _data_start = .;
        *(.data .data.*)
        /* The bootloader writes request responses into these structs, so
         * they must sit in a writable (loaded) segment. */
        . = ALIGN(8);
        KEEP(*(.limine_requests_start_marker))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end_marker))
        . = ALIGN(8);
        _ctors_start = .;
        KEEP(*(.init_array .init_array.*))
        _ctors_end = .;
        . = ALIGN(4K);
        _data_end = .;
    } :high_rw

    .bss : {
        _bss_start = .;
        *(.early_bss .early_bss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4K);
        _bss_end = .;
        _kernel_end = .;
    } :high_rw

    /DISCARD/ : {
        *(.multiboot)
        *(.note .note.*)
        *(.early_text .early_text.*)
        *(.early_data .early_data.*)
        *(.comment)
        *(.eh_frame)
        *(.gcc_except_table)
    }
}