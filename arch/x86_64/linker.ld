/* NEWOS x86_64 linker script
 *
 * The kernel is linked at a high-half virtual base (KERNEL_BASE) so that
 * user space can use the entire lower 47-bit canonical half. The boot
 * loader loads the image at PHYS_BASE (physical). Each section's LMA is
 * therefore VMA - KERNEL_BASE + PHYS_BASE.
 */

ENTRY(_start)

PHYS_BASE    = 0x100000;           /* physical load address (1 MiB)       */
KERNEL_BASE  = 0xffffffff80000000; /* high-half virtual link base         */

/* Explicit program headers: keep execute-only sections out of the data
 * segments so no PT_LOAD comes out with RWE. The low identity-mapped
 * trampoline is RX; its BSS page tables sit in a low RW segment; the
 * high-half kernel is split into an RX text segment and an RW data/
 * bss segment.
 *
 * FLAGS: 5 = PF_R|PF_X, 6 = PF_R|PF_W                                  */
PHDRS
{
    low_rx  PT_LOAD FLAGS(5);
    note    PT_NOTE FLAGS(4);
    low_rw  PT_LOAD FLAGS(6);
    high_rx PT_LOAD FLAGS(5);
    high_rw PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* The first bytes of the image are the multiboot2 header, requested by
     * the boot loader before the kernel is mapped at its final address. */
    . = PHYS_BASE;

    .multiboot : {
        KEEP(*(.multiboot))
    } :low_rx

    .note.Xen : {
        KEEP(*(.note.Xen))
    } :note :low_rx

    . = ALIGN(4K);

    /* Early trampoline code and data run at identity-mapped (physical)
     * addresses while long mode is being established. They must be low. */
    .early_text : {
        *(.early_text .early_text.*)
    } :low_rx
    .early_data : {
        *(.early_data .early_data.*)
    } :low_rx
    .early_bss (NOLOAD) : {
        *(.early_bss .early_bss.*)
    } :low_rw

    . = ALIGN(4K);
    _kernel_physical_load = .;

    /* Transition into high-half linkage. */
    . = KERNEL_BASE + (. - PHYS_BASE);

    .text : AT(ADDR(.text) - KERNEL_BASE + PHYS_BASE) {
        _text_start = .;
        *(.text .text.*)
        *(.init .init.*)
        *(.fini)
        . = ALIGN(4K);
        _text_end = .;
    } :high_rx

    .rodata : AT(ADDR(.rodata) - KERNEL_BASE + PHYS_BASE) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        *(.gcc_except_table)
        . = ALIGN(4K);
        _rodata_end = .;
    } :high_rx

    .data : AT(ADDR(.data) - KERNEL_BASE + PHYS_BASE) {
        _data_start = .;
        *(.data .data.*)
        . = ALIGN(8);
        _ctors_start = .;
        KEEP(*(.init_array .init_array.*))
        _ctors_end = .;
        KEEP(*(.fini_array .fini_array.*))
        . = ALIGN(4K);
        _data_end = .;
    } :high_rw

    .bss : AT(ADDR(.bss) - KERNEL_BASE + PHYS_BASE) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4K);
        _bss_end = .;
        _kernel_end = .;
    } :high_rw

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}