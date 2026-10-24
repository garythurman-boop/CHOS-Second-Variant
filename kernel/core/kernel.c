#include <stdint.h>
#include "chos/fdt.h"
#include "chos/platform.h"
#include "chos/uart.h"
#include "chos/shell.h"
#include "chos/pmm.h"
#include "chos/kheap.h"
#include "chos/interrupt.h"
#include "chos/timer.h"
#include "chos/vmm.h"
#include "chos/scheduler.h"
#include "chos/storage.h"
#include "chos/virtio_gpu.h"
#include "chos/virtio_input.h"
#include "chos/gui.h"
#include "chos/audio.h"
#include "chos/virtio_net.h"
#include "chos/net_stack.h"
#include "chos/boot_info.h"
#include "chos/acpi.h"
#include "chos/pci.h"
#include "chos/nvme.h"
#include "chos/entropy.h"
#include "../platform/uefi_console.h"

static fdt_info_t boot_device_tree;
extern uint8_t __kernel_start;

/* The VirtIO-MMIO driver uses the QEMU virt aperture. Do not touch that fixed
 * address range on arbitrary UEFI hardware: enable this path only when the
 * firmware FDT explicitly describes at least one VirtIO-MMIO node in range. */
static uint32_t uefi_fdt_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

/* Restrict VirtIO-MMIO probing to the aperture described by a validated FDT.
 * This prevents a bare-metal fallback address from being dereferenced on
 * generic UEFI hardware that merely happens to use ARM64. */
static int fdt_device_is_qemu_virtio_mmio(const fdt_device_t *device) {
    static const char compatible[] = "virtio,mmio";
    size_t i;
    if (device == NULL || !device->has_reg ||
        device->reg_base < UINT64_C(0x0a000000) ||
        device->reg_base >= UINT64_C(0x0a004000) ||
        device->reg_size < UINT64_C(0x200)) return 0;
    for (i = 0u; i < sizeof(compatible); ++i) {
        if (device->compatible[i] != compatible[i]) return 0;
    }
    return 1;
}

static int fdt_info_has_qemu_virtio_mmio(const fdt_info_t *fdt) {
    size_t i;
    if (fdt == NULL || !fdt->valid) return 0;
    for (i = 0u; i < fdt->device_count; ++i)
        if (fdt_device_is_qemu_virtio_mmio(&fdt->devices[i])) return 1;
    return 0;
}

static int uefi_fdt_has_virtio_mmio(const chos_boot_info_t *boot_info) {
    const uint8_t *bytes;
    fdt_info_t fdt;
    uint32_t total_size;
    size_t i;
    if (boot_info == NULL || (boot_info->flags & CHOS_BOOT_FLAG_FDT) == 0u ||
        boot_info->fdt_address == 0u ||
        !chos_boot_info_contains_memory(boot_info, boot_info->fdt_address, 40u))
        return 0;
    bytes = (const uint8_t *)(uintptr_t)boot_info->fdt_address;
    total_size = uefi_fdt_read_be32(bytes + 4u);
    /* Validate the entire claimed FDT range against a preserved EFI RAM
     * descriptor before the generic parser follows offsets into the blob. */
    if (total_size < 40u || total_size > (16u * 1024u * 1024u) ||
        !chos_boot_info_contains_memory(boot_info, boot_info->fdt_address, total_size) ||
        fdt_parse_blob(bytes, &fdt) != FDT_OK)
        return 0;
    for (i = 0u; i < fdt.device_count; ++i)
        if (fdt_device_is_qemu_virtio_mmio(&fdt.devices[i])) return 1;
    return 0;
}

/* Optional PMM progress hook. The UEFI console silently ignores writes before
 * GOP has initialized, so this stays harmless in the normal QEMU boot path. */
void pmm_init_progress(pmm_init_stage_t stage) {
    switch (stage) {
        case PMM_INIT_STAGE_UEFI_START:
            uefi_console_write("[PMM UEFI 1/3] Validating preserved boot map and reservations...\n");
            break;
        case PMM_INIT_STAGE_UEFI_MAP_NORMALIZED:
            uefi_console_write("[PMM UEFI 2/3] EFI RAM descriptors normalized.\n");
            break;
        case PMM_INIT_STAGE_UEFI_CORE_ENTRY:
            uefi_console_write("[PMM UEFI 3/3] Entering common allocator initializer.\n");
            break;
        case PMM_INIT_STAGE_VALIDATING:
            uefi_console_write("[PMM 1/5] Validating RAM descriptors and reservations...\n");
            break;
        case PMM_INIT_STAGE_REGIONS_READY:
            uefi_console_write("[PMM 2/5] RAM ranges validated; preparing active metadata maps...\n");
            break;
        case PMM_INIT_STAGE_BITMAPS_READY:
            uefi_console_write("[PMM 3/5] Active allocation metadata initialized.\n");
            break;
        case PMM_INIT_STAGE_RESERVING:
            uefi_console_write("[PMM 4/5] Reserving kernel, boot metadata and framebuffer...\n");
            break;
        case PMM_INIT_STAGE_RESERVATIONS_DONE:
            uefi_console_write("[PMM 5/5] Reservations applied; checking allocator counters...\n");
            break;
        case PMM_INIT_STAGE_COUNTERS_OK:
            uefi_console_write("[PMM 6/6] Allocator counters consistent; publishing state...\n");
            break;
        case PMM_INIT_STAGE_CORE_READY:
            uefi_console_write("[OK] PMM initialization complete; returning to kernel.\n");
            break;
        default:
            break;
    }
}
extern uint8_t __kernel_end;

static void print_banner(void) {
    uart_puts("\n");
    uart_puts("  ____ _   _  ___  ____\n");
    uart_puts(" / ___| | | |/ _ \\/ ___|\n");
    uart_puts("| |   | |_| | | | \\___ \\\n");
    uart_puts("| |___|  _  | |_| |___) |\n");
    uart_puts(" \\____|_| |_|\\___/|____/\n");
    uart_puts("CHOS ARM64 kernel 0.30.0\n");
}

static __attribute__((noreturn)) void kernel_halt_safely(void) {
    for (;;) __asm__ volatile("wfe");
}

void kernel_main(uintptr_t boot_hint, uint64_t boot_source_magic) {
    fdt_error_t fdt_result;
    pmm_error_t pmm_result;
    vmm_error_t vmm_result;

    /* UEFI uses the preserved firmware map and GOP directly. Native PCI storage
     * is independently discovered through ACPI/ECAM. The existing polled
     * VirtIO-MMIO input backend is enabled only when the firmware FDT confirms
     * the QEMU virt transport aperture; no fixed MMIO address is touched on an
     * arbitrary UEFI machine. Native PCI xHCI/HID remains a separate driver task. */
    if (boot_source_magic == CHOS_UEFI_ENTRY_MAGIC) {
        const chos_boot_info_t *boot_info = (const chos_boot_info_t *)(uintptr_t)boot_hint;
        chos_acpi_info_t acpi_info;
        chos_acpi_error_t acpi_result;
        chos_pci_device_t pci_devices[CHOS_PCI_MAX_DEVICES];
        chos_pci_scan_summary_t pci_summary;
        pmm_stats_t memory_stats;
        chos_surface_t surface;
        chos_block_device_t nvme_block;
        int console_ready;
        int gic_timer_ready = 0;
        int input_ready = 0;
        uint32_t pci_device_count = 0u, xhci_count = 0u;
        uint16_t xhci_vendor = 0u, xhci_device_id = 0u;
        uint32_t xhci_bar0 = 0u, xhci_bar1 = 0u;

        /* Boot Services are exited by the loader. The kernel owns exception
         * vectors and hardware from this point onward; keep IRQs masked for setup. */
        interrupt_disable_irqs();
        /* The EFI loader starts a fresh single-core bootstrap phase. Reset and
         * publish the heap's lock-free mode before any heap/GUI initialization. */
        kheap_begin_bootstrap();
        if (!chos_boot_info_validate(boot_info)) kernel_halt_safely();
        console_ready = uefi_console_init(boot_info);
        if (console_ready) {
            uefi_console_write("[BOOT] Heap bootstrap mode explicitly initialized; runtime heap lock bypassed until startup completes.\n");
            uefi_console_write("[OK] Versioned UEFI boot-information ABI validated.\n");
            uefi_console_write("[OK] Kernel ELF loaded and relocated; kernel entry reached.\n\n");
            uefi_console_report_boot_info(boot_info);
        }

        acpi_result = chos_acpi_parse(boot_info, &acpi_info);
        if (console_ready) {
            uefi_console_write("\nACPI parser: "); uefi_console_write(chos_acpi_error_string(acpi_result)); uefi_console_write("\n");
            if (acpi_result == CHOS_ACPI_OK) {
                uefi_console_write("GIC distributor base: "); uefi_console_write_hex(acpi_info.gic_distributor_base); uefi_console_write("\n");
                uefi_console_write("GIC redistributor base: "); uefi_console_write_hex(acpi_info.gic_redistributor_base); uefi_console_write("\n");
                uefi_console_write("Physical timer GSIV: "); uefi_console_write_u64(acpi_info.physical_timer_irq); uefi_console_write("\n");
                uefi_console_write("PCI ECAM segments: "); uefi_console_write_u64(acpi_info.pci_segment_count); uefi_console_write("\n");
                if (acpi_info.pci_segment_count != 0u) {
                    uefi_console_write("PCI ECAM base: "); uefi_console_write_hex(acpi_info.pci_segments[0].ecam_base); uefi_console_write("\n");
                }
            }
            uefi_console_write("\nInitializing physical page allocator from EFI conventional memory...\n");
        }

        /* Read-only PCI inventory. ACPI MCFG is the authoritative ECAM aperture;
         * the scanner now supports firmware maps that omit ECAM, while rejecting
         * every overlap with RAM-backed or unknown memory descriptors. */
        if (acpi_result == CHOS_ACPI_OK && acpi_info.pci_segment_count != 0u) {
            if (chos_pci_enumerate(boot_info, &acpi_info, pci_devices,
                                   CHOS_PCI_MAX_DEVICES, &pci_summary)) {
                uint32_t pci_index;
                pci_device_count = pci_summary.devices_found;
                for (pci_index = 0u; pci_index < pci_summary.devices_stored; ++pci_index) {
                    const chos_pci_device_t *device = &pci_devices[pci_index];
                    if (device->class_code == 0x0cu && device->subclass == 0x03u &&
                        device->programming_interface == 0x30u) {
                        if (xhci_count == 0u) {
                            xhci_vendor = device->vendor_id;
                            xhci_device_id = device->device_id;
                            xhci_bar0 = device->bars[0];
                            xhci_bar1 = device->bars[1];
                        }
                        ++xhci_count;
                    }
                }
                if (console_ready) {
                    uefi_console_write("PCI ECAM inventory: found ");
                    uefi_console_write_u64(pci_summary.devices_found);
                    uefi_console_write(" devices; scanned buses: ");
                    uefi_console_write_u64(pci_summary.buses_scanned);
                    uefi_console_write("; skipped unmapped windows: ");
                    uefi_console_write_u64(pci_summary.buses_skipped_unmapped);
                    uefi_console_write("\n");
                    for (uint32_t pci_index = 0u;
                         pci_index < pci_summary.devices_stored && pci_index < 16u;
                         ++pci_index) {
                        const chos_pci_device_t *device = &pci_devices[pci_index];
                        uefi_console_write("PCI ");
                        uefi_console_write_u64(device->segment); uefi_console_write(":");
                        uefi_console_write_hex(device->bus); uefi_console_write(":");
                        uefi_console_write_hex(device->device); uefi_console_write(".");
                        uefi_console_write_hex(device->function); uefi_console_write(" id ");
                        uefi_console_write_hex(device->vendor_id); uefi_console_write(":");
                        uefi_console_write_hex(device->device_id); uefi_console_write(" class ");
                        uefi_console_write_hex(device->class_code); uefi_console_write(":");
                        uefi_console_write_hex(device->subclass); uefi_console_write("\n");
                        if (device->class_code == 0x0cu && device->subclass == 0x03u &&
                            device->programming_interface == 0x30u) {
                            uefi_console_write("  xHCI BAR0="); uefi_console_write_hex(device->bars[0]);
                            uefi_console_write(" BAR1="); uefi_console_write_hex(device->bars[1]);
                            uefi_console_write(" command="); uefi_console_write_hex(device->command);
                            uefi_console_write(" (read-only; driver not attached)\n");
                        }
                    }
                    if (pci_summary.output_truncated != 0u)
                        uefi_console_write("PCI inventory truncated to first 64 devices.\n");
                }
            } else if (console_ready) {
                uefi_console_write("[WARN] PCI inventory skipped: invalid boot-info or MCFG metadata.\n");
            }
        }

        chos_gui_set_pci_input_status(pci_device_count, xhci_count, xhci_vendor,
                                      xhci_device_id, xhci_bar0, xhci_bar1);
        if (console_ready) uefi_console_write("[PMM] Building allocator from RAM ranges and compact reservations...\n");
        pmm_result = pmm_init_from_uefi(boot_info);
        if (pmm_result != PMM_OK) {
            if (console_ready) {
                uefi_console_write("[FAIL] EFI memory map could not initialize PMM: ");
                uefi_console_write(pmm_error_string(pmm_result)); uefi_console_write("\n");
            }
            kernel_halt_safely();
        }
        if (console_ready) uefi_console_write("[OK] PMM returned to kernel; collecting bootstrap statistics without runtime locks...\n");
        if (!pmm_is_bootstrap_mode()) {
            if (console_ready) uefi_console_write("[FAIL] EFI PMM bootstrap-lock mode was not active.\n");
            kernel_halt_safely();
        }
        pmm_get_stats(&memory_stats);
        if (console_ready) uefi_console_write("[OK] PMM bootstrap statistics snapshot acquired.\n");
        if (console_ready) {
            uefi_console_write("[OK] Physical page allocator initialized. Free pages: ");
            uefi_console_write_u64(memory_stats.free_pages); uefi_console_write("; regions: ");
            uefi_console_write_u64(memory_stats.region_count); uefi_console_write("\n");
        }
        if (console_ready) uefi_console_write("[BOOT] Entering lock-free heap bootstrap initialization...\n");
        if (!kheap_init()) {
            if (console_ready) uefi_console_write("[FAIL] Kernel heap could not attach to PMM.\n");
            kernel_halt_safely();
        }
        if (console_ready) uefi_console_write("[OK] Kernel heap initialized.\n");

        /* UEFI has no VirtIO-MMIO storage assumptions: attach a native PCI NVMe
         * namespace when one is present. It is never formatted automatically. */
        if (console_ready) uefi_console_write("[BOOT] Probing PCI NVMe storage controller...\n");
        if (chos_nvme_init(boot_info, &acpi_info, &nvme_block)) {
            if (console_ready) {
                uefi_console_write("[OK] Native NVMe namespace online: sectors=");
                uefi_console_write_u64(nvme_block.sector_count);
                uefi_console_write(" (512-byte logical blocks).\n");
            }
            if (storage_attach_block_device(&nvme_block)) {
                if (storage_mounted()) {
                    if (console_ready) uefi_console_write("[OK] CHFS filesystem mounted on NVMe.\n");
                } else if (console_ready) {
                    uefi_console_write("[INFO] NVMe disk has no mountable CHFS at LBA 0; left unchanged (no auto-format).\n");
                }
            } else if (console_ready) {
                uefi_console_write("[WARN] NVMe was detected but could not be attached to the filesystem layer.\n");
            }
        } else if (console_ready) {
            uefi_console_write("[INFO] Native NVMe unavailable: ");
            uefi_console_write(chos_nvme_last_error());
            uefi_console_write("; UEFI desktop continues without disk writes.\n");
        }

        if (console_ready) uefi_console_write("[BOOT] Initializing scheduler in bootstrap mode...\n");
        scheduler_init();
        if (console_ready) uefi_console_write("[OK] Scheduler initialization returned.\n");

        if (console_ready) uefi_console_write("[BOOT] Initializing platform interrupt/timer path with IRQs masked...\n");
        if (acpi_result == CHOS_ACPI_OK && interrupt_controller_init_uefi(boot_info, &acpi_info)) {
            if (timer_init() && interrupt_enable_timer_source()) {
                gic_timer_ready = 1;
                if (console_ready) uefi_console_write("[OK] ACPI-described GICv3 and physical timer initialized; IRQs stay masked until desktop is ready.\n");
            } else {
                timer_stop();
                interrupt_disable_irqs();
                if (console_ready) uefi_console_write("[WARN] GICv3 available but timer setup failed; continuing with polled GUI and IRQs masked.\n");
            }
        } else if (console_ready) {
            uefi_console_write("[WARN] GICv3 initialization unavailable; continuing with polled GUI and IRQs masked.\n");
        }

        if (console_ready) uefi_console_write("[BOOT] Initializing GOP desktop surface...\n");
        if (!virtio_gpu_init_gop(boot_info, &surface)) {
            if (console_ready) {
                uefi_console_write("[FAIL] GOP desktop backend unavailable: ");
                uefi_console_write(virtio_gpu_last_error()); uefi_console_write("\n");
            }
            kernel_halt_safely();
        }
        if (console_ready) uefi_console_write("[OK] GOP-backed CHOS display surface initialized.\n");
        if (console_ready) uefi_console_write("[BOOT] Initializing CHOS GUI/compositor...\n");
        if (!chos_gui_init(&surface)) {
            /* The compositor writes a detailed backend reason when possible. */
            if (console_ready) {
                uefi_console_write("[FAIL] CHOS desktop compositor initialization failed: ");
                uefi_console_write(virtio_gpu_last_error()); uefi_console_write("\n");
            }
            kernel_halt_safely();
        }
        /* The UEFI path previously rendered a usable-looking desktop but never
         * initialized the existing polled input driver. Only probe the fixed
         * VirtIO-MMIO aperture when the validated firmware FDT advertises it;
         * never dereference QEMU addresses on an arbitrary Fusion/UEFI host. */
        if (uefi_fdt_has_virtio_mmio(boot_info)) {
            input_ready = chos_input_init() > 0;
            if (console_ready) {
                if (input_ready) uefi_console_write("[OK] FDT-confirmed VirtIO input queues initialized.\n");
                else {
                    uefi_console_write("[WARN] FDT advertises VirtIO-MMIO but input queues failed: ");
                    uefi_console_write(chos_input_last_error()); uefi_console_write("\n");
                }
                if (chos_entropy_init()) uefi_console_write("[OK] VirtIO RNG initialized; nonblocking hardware-backed entropy is available.\n");
                else { uefi_console_write("[ .. ] VirtIO RNG unavailable: "); uefi_console_write(chos_entropy_last_error()); uefi_console_write("\n"); }
            } else {
                (void)chos_entropy_init();
            }
        } else if (console_ready) {
            uefi_console_write("[INFO] No verified VirtIO-MMIO input aperture in FDT; native PCI USB HID is not attached.\n");
        }
        chos_gui_startup_complete();
        chos_gui_show();
        if (console_ready) uefi_console_write("[OK] Initial CHOS GUI frame rendered.\n");
        /* All allocator work so far is single-core and IRQ-masked. Normalize
         * both lock states before publishing runtime mode and enabling IRQs. */
        kheap_enable_runtime_locking();
        pmm_enable_runtime_locking();
        if (console_ready) uefi_console_write("[OK] Heap and PMM runtime locking enabled after bootstrap.\n");
        if (console_ready) uefi_console_write("[OK] PMM runtime locking enabled after bootstrap.\n");
        if (gic_timer_ready) {
            interrupt_enable_irqs();
            if (console_ready) uefi_console_write("[OK] IRQ delivery enabled after initial desktop frame.\n");
        }
        for (;;) {
            if (input_ready) chos_input_poll();
            else chos_gui_pump();
            __asm__ volatile("yield" ::: "memory");
        }
    }

    platform_set_boot_dtb_hint(boot_hint);
    uart_init();
    scheduler_init();
    print_banner();
    uart_puts("[ OK ] C kernel entry reached\n");
    uart_puts("[ OK ] Bootstrap stack active\n");
    uart_puts("[ OK ] PL011 serial console initialized\n");
    uart_puts("[ OK ] AArch64 exception vectors installed at EL1\n");
    uart_puts("[ OK ] Early EL1 alignment-check state normalized\n");
    uart_puts("[ OK ] Cooperative scheduler initialized; deferred task cleanup enabled\n");
    uart_puts("[ OK ] IRQ return-frame ABI ready; scheduled runtime available when GUI/timer are ready\n");
    uart_puts("[ OK ] IRQ-safe spinlock, PMM/heap locking and serialized PL011 console writes initialized\n");
    uart_puts("[ OK ] Lower-EL synchronous exception path ready for controlled SVC dispatch\n");
    uart_puts("[ OK ] CHCX executable loader and foreground process cleanup framework compiled in\n");

    fdt_result = fdt_parse_blob((const void *)platform_dtb_address(), &boot_device_tree);
    if (fdt_result == FDT_OK) {
        uart_puts("[ OK ] Device tree parsed\n");
        pmm_result = pmm_init(&boot_device_tree,
                              (uint64_t)(uintptr_t)&__kernel_start,
                              (uint64_t)(uintptr_t)&__kernel_end,
                              (uint64_t)platform_dtb_address(),
                              (uint64_t)boot_device_tree.total_size);
        if (pmm_result == PMM_OK) {
            uart_puts("[ OK ] Physical page allocator initialized\n");
            uart_puts("[ OK ] RAM reserved: kernel image + device tree + FDT static reservations\n");
            if (kheap_init()) {
                uart_puts("[ OK ] Kernel heap initialized (lazy page-backed arenas)\n");
            } else {
                uart_puts("[WARN] Kernel heap initialization failed\n");
            }
        } else {
            uart_puts("[WARN] Physical page allocator unavailable: ");
            uart_puts(pmm_error_string(pmm_result));
            uart_puts("\n");
        }
    } else {
        uart_puts("[WARN] Device tree parse failed: ");
        uart_puts(fdt_error_string(fdt_result));
        uart_puts("\n");
        uart_puts("[ .. ] Physical memory size is unknown\n");
    }
    if (fdt_result == FDT_OK) {
        vmm_result = vmm_prepare(&boot_device_tree,
                                 (uint64_t)(uintptr_t)&__kernel_start,
                                 (uint64_t)(uintptr_t)&__kernel_end,
                                 (uint64_t)platform_dtb_address(),
                                 (uint64_t)boot_device_tree.total_size);
        if (vmm_result == VMM_OK) {
            uart_puts("[ OK ] 4 KiB-granule EL1 page tables prepared\n");
            uart_puts("[ OK ] RAM and required platform MMIO pages validated\n");
            uart_puts("[ OK ] bootstrap and per-task stack guard pages reserved as unmapped\n");
            if (vmm_enable()) {
                uart_puts("[ OK ] MMU enabled; fine-grained QEMU virt map active\n");
                if (vmm_self_test()) {
                    uart_puts("[ OK ] Post-enable translation, dynamic map/unmap and RAM alias probe passed\n");
                } else {
                    uart_puts("[WARN] Post-enable VMM self-test failed\n");
                }
            } else {
                uart_puts("[WARN] MMU not enabled: ");
                uart_puts(vmm_error_string(vmm_last_error()));
                uart_puts("\n");
            }
        } else {
            uart_puts("[WARN] MMU bootstrap map unavailable: ");
            uart_puts(vmm_error_string(vmm_result));
            uart_puts("; continuing with current translation state\n");
        }
    }

    if (fdt_info_has_qemu_virtio_mmio(&boot_device_tree)) {
        if (chos_entropy_init()) uart_puts("[ OK ] VirtIO RNG initialized; nonblocking hardware-backed entropy is available\n");
        else { uart_puts("[ .. ] VirtIO RNG unavailable: "); uart_puts(chos_entropy_last_error()); uart_puts("\n"); }
    } else {
        uart_puts("[ .. ] No FDT-confirmed VirtIO-MMIO aperture; entropy driver not probed\n");
    }

    if (interrupt_controller_init()) {
        uart_puts("[ OK ] GICv2 distributor and CPU interface initialized\n");
        if (timer_init()) {
            uart_puts("[ OK ] ARM Generic Physical Timer configured at 100 Hz\n");
            interrupt_enable_irqs();
            uart_puts("[ OK ] IRQ delivery enabled; synchronous exceptions remain fatal\n");
        } else {
            uart_puts("[WARN] ARM Generic Timer unavailable; IRQs remain masked\n");
        }
    } else {
        uart_puts("[WARN] GICv2 initialization failed; IRQs remain masked\n");
    }

    if (storage_init()) {
        uart_puts("[ OK ] VirtIO block device discovered (polled I/O)\n");
        if (storage_mounted()) uart_puts("[ OK ] CHFS persistent filesystem mounted\n");
        else uart_puts("[WARN] Disk is unformatted or CHFS metadata is damaged; use 'mkfs' only for a new/replaceable disk\n");
    } else {
        uart_puts("[WARN] VirtIO block device unavailable: ");
        uart_puts(storage_error());
        uart_puts("; embedded ROMFS remains available\n");
    }

    if (virtio_net_init()) {
        chos_virtio_net_info_t net;
        virtio_net_get_info(&net);
        uart_puts("[ OK ] VirtIO-net Ethernet queues initialized; MAC=");
        {
            static const char hex[] = "0123456789abcdef";
            size_t i;
            for (i = 0u; i < CHOS_NET_MAC_BYTES; ++i) {
                uart_putc(hex[(net.mac[i] >> 4) & 0x0fu]);
                uart_putc(hex[net.mac[i] & 0x0fu]);
                if (i + 1u < CHOS_NET_MAC_BYTES) uart_putc(':');
            }
        }
        uart_puts(net.link_status_known ? (net.link_up ? " link=up\n" : " link=down\n") : " link=unknown\n");
        chos_net_stack_init();
        uart_puts("[ OK ] Non-blocking DHCPv4 client initialized; configuration proceeds in shell poll loop\n");
        uart_puts("[ OK ] Non-blocking DHCP, DNS A lookup, ARP and ICMP diagnostics initialized; TCP sockets/TLS remain future work\n");
    } else {
        uart_puts("[ .. ] Optional VirtIO-net unavailable: ");
        uart_puts(virtio_net_last_error());
        uart_puts("; boot continues offline\n");
    }

    if (chos_audio_init()) {
        uart_puts("[ OK ] Optional VirtIO-Sound playback initialized (48 kHz stereo)\n");
    } else {
        uart_puts("[ .. ] Optional VirtIO-Sound unavailable: ");
        uart_puts(chos_audio_last_error());
        uart_puts("; startup remains silent\n");
    }

    {
        chos_surface_t surface;
        if (virtio_gpu_init(&surface)) {
            uart_puts("[ OK ] VirtIO-GPU control queue ready; 800x600 framebuffer attached\n");
            if (chos_gui_init(&surface)) {
                uart_puts("[ OK ] CHOS desktop composed and presented\n");
                {
                    int input_devices = chos_input_init();
                    if (input_devices > 0) {
                        uart_puts("[ OK ] VirtIO input devices initialized (polled event queues)\n");
                    } else {
                        uart_puts("[WARN] VirtIO input unavailable: ");
                        uart_puts(chos_input_last_error());
                        uart_puts("; use the serial console\n");
                    }
                }
            } else {
                uart_puts("[WARN] CHOS desktop compositor could not present the initial frame: ");
                uart_puts(virtio_gpu_last_error());
                uart_puts("; serial console remains available\n");
            }
        } else {
            uart_puts("[WARN] VirtIO-GPU unavailable: ");
            uart_puts(virtio_gpu_last_error());
            uart_puts("; text console remains available\n");
        }
    }

    /* The splash may only leave once the minimum presentation interval and the
     * remaining platform/device initialization have both completed. */
    chos_gui_startup_complete();

    uart_puts("\n");
    shell_run(&boot_device_tree);
}
