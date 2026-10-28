OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); constant PT_LOAD FLAGS(4); writable PT_LOAD FLAGS(6); abi PT_NOTE FLAGS(4); }
SECTIONS {
 . = 0x20000000;
 .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
 __lcxx_override : { __start___lcxx_override = .; KEEP(*(__lcxx_override)) __stop___lcxx_override = .; } :code
 . = ALIGN(4096);
 .rodata : { *(.rodata .rodata.*) } :constant
 .preinit_array ALIGN(8) : { __preinit_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.preinit_array.*))) KEEP(*(.preinit_array)) __preinit_array_end = .; } :constant
 .init_array ALIGN(8) : { __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } :constant
 .fini_array ALIGN(8) : { __fini_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .; } :constant
 .unsupported_legacy_ctors : { *(.ctors .ctors.* .dtors .dtors.*) } :constant
 ASSERT(SIZEOF(.unsupported_legacy_ctors) == 0,"Legacy ctor/dtor lists are unsupported; use init/fini arrays")
 .note.chos : { KEEP(*(.note.chos)) } :abi
 . = ALIGN(4096);
 .data : { *(.data .data.*) *(.got .got.*) } :writable
 .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :writable
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.llvm_addrsig) }
 ASSERT(. <= 0x20400000,"Native application exceeds bounded4MiB arena")
 ASSERT(SIZEOF(.note.chos) == 24,"Native ELF requires a CHOS ABI note")
}
