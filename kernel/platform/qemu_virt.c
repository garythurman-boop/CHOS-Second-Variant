#include <stdint.h>
#include "chos/platform.h"

/*
 * QEMU virt puts the generated DTB at the start of RAM for bare-metal ELF
 * boots. The generic loader sets the PC but does not promise an FDT pointer
 * in x0, so this platform adapter provides the documented location explicitly.
 */
#define QEMU_VIRT_DTB_ADDRESS ((uintptr_t)0x40000000u)
#define QEMU_VIRT_UART_BASE    ((uintptr_t)0x09000000u)
#define QEMU_VIRT_GICD_BASE   ((uintptr_t)0x08000000u)
#define QEMU_VIRT_GICC_BASE   ((uintptr_t)0x08010000u)
#define QEMU_VIRT_VIRTIO_BLK_BASE ((uintptr_t)0x0a000000u)
#define QEMU_VIRT_RTC_BASE      ((uintptr_t)0x09010000u)
#define RTC_EPOCH_MIN           1577836800u /* 2020-01-01 */
#define RTC_EPOCH_MAX           4102444800u /* 2100-01-01 */

static uintptr_t boot_dtb_address;

static uint32_t fdt_be32(const volatile uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

void platform_set_boot_dtb_hint(uintptr_t address) {
    volatile const uint8_t *bytes;
    uint32_t total_size;
    /* A firmware-supplied FDT need not sit inside QEMU's usual RAM window.
     * Trust only a non-null handoff after checking alignment, magic, size and
     * integer overflow; the UEFI boot-info path validates its own ABI first. */
    if (address == 0u || (address & 7u) != 0u || address > UINTPTR_MAX - 8u)
        return;
    bytes = (volatile const uint8_t *)address;
    if (fdt_be32(bytes) != 0xd00dfeedu) return;
    total_size = fdt_be32(bytes + 4u);
    if (total_size < 40u || total_size > 1024u * 1024u ||
        address + (uintptr_t)total_size < address) return;
    boot_dtb_address = address;
}

uintptr_t platform_dtb_address(void) {
    return boot_dtb_address != 0u ? boot_dtb_address : QEMU_VIRT_DTB_ADDRESS;
}

const char *platform_name(void) {
    return "QEMU virt (AArch64)";
}

/* This build currently uses QEMU-virt-compatible device addresses; FDT
 * relocation support does not by itself provide non-QEMU device drivers. */
uintptr_t platform_uart_base(void) {
    return QEMU_VIRT_UART_BASE;
}

uintptr_t platform_rtc_base(void) {
    return QEMU_VIRT_RTC_BASE;
}

uintptr_t platform_gic_distributor_base(void) {
    return QEMU_VIRT_GICD_BASE;
}

uintptr_t platform_gic_cpu_interface_base(void) {
    return QEMU_VIRT_GICC_BASE;
}

/* Base of the QEMU virt MMIO transport aperture (32 slots x 0x200 bytes).
   Drivers scan slots because QEMU does not promise a stable device-slot assignment. */
uintptr_t platform_virtio_mmio_base(void) {
    return QEMU_VIRT_VIRTIO_BLK_BASE;
}

/* QEMU virt exposes a PL031-compatible RTC at 0x09010000 (RTCDR at +0). */
uint64_t platform_rtc_unix_seconds(void) {
    volatile const uint32_t *rtc_data = (volatile const uint32_t *)QEMU_VIRT_RTC_BASE;
    uint32_t seconds_a = rtc_data[0];
    uint32_t seconds_b = rtc_data[0];
    uint32_t seconds = seconds_b >= seconds_a ? seconds_b : rtc_data[0];
    if (seconds < RTC_EPOCH_MIN || seconds > RTC_EPOCH_MAX) return 0u;
    return (uint64_t)seconds;
}
