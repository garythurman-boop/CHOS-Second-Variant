OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); constant PT_LOAD FLAGS(4); writable PT_LOAD FLAGS(6); abi PT_NOTE FLAGS(4); }
SECTIONS {
    . = 0x20000000;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
    . = 0x20010000;
    .rodata : { *(.rodata .rodata.*) } :constant
    .note.chos : { KEEP(*(.note.chos)) } :abi
    . = 0x20080000;
    .data : { *(.data .data.*) *(.got .got.*) } :writable
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :writable
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.llvm_addrsig) }
    ASSERT(. <= 0x20200000,"Native test exceeds bounded loader arena")
    ASSERT(SIZEOF(.note.chos) == 24,"Native ELF requires a CHOS ABI note")
}
