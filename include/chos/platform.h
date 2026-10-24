#ifndef CHOS_PLATFORM_H
#define CHOS_PLATFORM_H

#include <stdint.h>

/* Address of the DTB supplied by the current bare-metal boot method. */
uintptr_t platform_dtb_address(void);
/* Accept a boot-loader FDT hint when it points at a plausible QEMU RAM FDT.
 * Invalid/absent hints retain the direct-boot QEMU fixed-address fallback. */
void platform_set_boot_dtb_hint(uintptr_t address);
const char *platform_name(void);
uintptr_t platform_uart_base(void);
uintptr_t platform_rtc_base(void);
uintptr_t platform_gic_distributor_base(void);
uintptr_t platform_gic_cpu_interface_base(void);
uintptr_t platform_virtio_mmio_base(void);
/* Unix epoch seconds from the platform RTC, or 0 when no valid clock is available. */
uint64_t platform_rtc_unix_seconds(void);

#endif
