/* Explicit diagnostic target only; staged by prepare_native_engine.py. */
.section .rodata.native_engine, "a", %progbits
.balign 16
.global chos_romfs_engine_start
.global chos_romfs_engine_end
chos_romfs_engine_start:
    .incbin CHOS_ENGINE_ELF_PATH
chos_romfs_engine_end:
