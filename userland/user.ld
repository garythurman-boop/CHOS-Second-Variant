OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : ALIGN(4)
    {
        __chos_code_start = .;
        KEEP(*(.text.boot))
        *(.text .text.*)
        __chos_code_end = .;
    }
    . = ALIGN(4096);
    .rodata :
    {
        __chos_rodata_start = .;
        *(.rodata .rodata.*)
        __chos_rodata_end = .;
    }
    . = ALIGN(4096);
    .data :
    {
        __chos_data_start = .;
        *(.data .data.*)
        *(.got .got.*)
        /* Keep the CHCX initialized-data boundary aligned with the following BSS section. */
        . = ALIGN(16);
        __chos_data_end = .;
    }
    .bss (NOLOAD) :
    {
        __chos_bss_start = .;
        *(.bss .bss.* COMMON)
        __chos_bss_end = .;
    }
    __chos_virtual_end = ALIGN(., 4096);
    .unsupported_startup_arrays : { *(.preinit_array .preinit_array.* .init_array .init_array.* .fini_array .fini_array.* .ctors .ctors.* .dtors .dtors.*) }
    ASSERT(SIZEOF(.unsupported_startup_arrays) == 0,"Global constructors require the dense native static-startup profile")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.llvm_addrsig) }
    ASSERT(__chos_code_start == 0, "CHCX code must begin at virtual offset zero")
    ASSERT(__chos_code_end > __chos_code_start, "CHCX code is empty")
    ASSERT(__chos_virtual_end <= 65536, "CHCX v2 image exceeds 16 pages")
}
