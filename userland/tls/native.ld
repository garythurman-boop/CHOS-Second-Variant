OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); constant PT_LOAD FLAGS(4); writable PT_LOAD FLAGS(6); abi PT_NOTE FLAGS(4); }
SECTIONS {
 . = 0x20000000;
 .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
 . = ALIGN(4096);
 .rodata : { *(.rodata .rodata.*) } :constant
 .note.chos : { KEEP(*(.note.chos)) } :abi
 . = ALIGN(4096);
 .data : { *(.data .data.*) *(.got .got.*) } :writable
 .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :writable
 .unsupported_startup_arrays : { *(.preinit_array .preinit_array.* .init_array .init_array.* .fini_array .fini_array.* .ctors .ctors.* .dtors .dtors.*) }
 ASSERT(SIZEOF(.unsupported_startup_arrays) == 0,"Global constructors require the dense native static-startup profile")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.llvm_addrsig) }
 ASSERT(. <= 0x20200000,"TLS client exceeds bounded native arena")
 ASSERT(SIZEOF(.note.chos) == 24,"Native ELF requires a CHOS ABI note")
}
