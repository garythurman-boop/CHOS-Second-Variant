OUTPUT_ARCH(aarch64)
ENTRY(_start)

/*
 * QEMU generic loader reserves 0x40000000..0x40100000 for its DTB. Keep
 * CHOS above it. Each kernel section ends on a 4 KiB boundary so the MMU can
 * give text, rodata, data and bss distinct page permissions.
 */
SECTIONS
{
    . = 0x40200000;
    __kernel_start = .;
    .text : ALIGN(4096) {
        __text_start = .;
        KEEP(*(.text.boot))
    *(.text .text.*)
        . = ALIGN(4096);
        __text_end = .;
    }
    .rodata : ALIGN(4096) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4096);
        __rodata_end = .;
    }
    .data : ALIGN(4096) {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4096);
        __data_end = .;
    }
    .bss (NOLOAD) : ALIGN(4096) {
        __bss_start = .;
        *(.bss.stack)
        . = ALIGN(4096);
        __task_stacks_start = .;
        KEEP(*(.bss.task_stacks))
        __task_stacks_end = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    }
    . = ALIGN(4096);
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
