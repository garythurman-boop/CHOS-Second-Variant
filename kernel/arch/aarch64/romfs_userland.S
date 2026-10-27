/* A real C application compiled and packed by the host-side userland pipeline. */
.section .rodata.chcx.userland, "a", %progbits
.balign 16
.global chos_romfs_cdemo_start
.global chos_romfs_cdemo_end
chos_romfs_cdemo_start:
    .incbin "build/userland/cdemo.chcx"
chos_romfs_cdemo_end:

.balign 16
.global chos_romfs_guiapp_start
.global chos_romfs_guiapp_end
chos_romfs_guiapp_start:
    .incbin "build/userland/gui_demo.chcx"
chos_romfs_guiapp_end:

.balign 16
.global chos_romfs_textview_start
.global chos_romfs_textview_end
chos_romfs_textview_start:
    .incbin "build/userland/text_viewer.chcx"
chos_romfs_textview_end:

.balign 16
.global chos_romfs_console_start
.global chos_romfs_console_end
chos_romfs_console_start:
    .incbin "build/userland/console.chcx"
chos_romfs_console_end:

.balign 16
.global chos_romfs_fileman_start
.global chos_romfs_fileman_end
chos_romfs_fileman_start:
    .incbin "build/userland/file_manager.chcx"
chos_romfs_fileman_end:

.balign 16
.global chos_romfs_browser_start
.global chos_romfs_browser_end
chos_romfs_browser_start:
    .incbin "build/userland/browser.chcx"
chos_romfs_browser_end:

/* Opt-in runtime verification, launched explicitly from Console. */
.balign 16
.global chos_romfs_stdio_start
.global chos_romfs_stdio_end
chos_romfs_stdio_start:
    .incbin "build/userland/stdio_test.elf"
chos_romfs_stdio_end:
