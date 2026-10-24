#ifndef CHOS_UEFI_CONSOLE_H
#define CHOS_UEFI_CONSOLE_H
#include <stdint.h>
#include "chos/boot_info.h"
int uefi_console_init(const chos_boot_info_t *info);
void uefi_console_write(const char *text);
void uefi_console_enable_serial(void);
void uefi_console_set_graphics_logging(int enabled);
const char *uefi_console_boot_log(void);
void uefi_console_write_hex(uint64_t value);
void uefi_console_write_u64(uint64_t value);
void uefi_console_report_boot_info(const chos_boot_info_t *info);
#endif
