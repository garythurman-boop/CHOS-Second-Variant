#include "chos/threads.h"
#include <stdint.h>
#include "chos/fdt.h"
#include "chos/platform.h"
#include "chos/power.h"
#include "chos/uart.h"
#include "chos/shell.h"
#include "chos/pmm.h"
#include "chos/kheap.h"
#include "chos/interrupt.h"
#include "chos/timer.h"
#include "chos/clock.h"
#include "chos/drivers.h"
#include "chos/driver_report.h"
#include "chos/vmm.h"
#include "chos/scheduler.h"
#include "chos/process.h"
#include "chos/storage.h"
#include "chos/virtio_gpu.h"
#include "chos/virtio_input.h"
#include "chos/gui.h"
#include "chos/audio.h"
#include "chos/virtio_net.h"
#include "chos/net_stack.h"
#include "chos/net_manager.h"
#include "chos/boot_info.h"
#include "chos/acpi.h"
#include "chos/pci.h"
#include "chos/nvme.h"
#include "chos/installer_io.h"
#include "chos/entropy.h"
#include "chos/seed_rng.h"
#include "chos/virtio_transport.h"
#include "chos/xhci.h"
#include "chos/build_stamp.h"
#include "../platform/uefi_console.h"

/* Native discovery is explicit from the GUI. Probe never formats a disk; the
 * filesystem receives a read-only view until a separate write authorization. */
static int installation_in_progress(void);
static const chos_boot_info_t *native_storage_boot;
static chos_acpi_info_t native_storage_acpi;
static chos_block_device_t native_storage_disk;
static uint64_t native_storage_selected_identity;
static int native_storage_writable;
int chos_probe_native_storage(void) {
    if(installation_in_progress())return 0;
    chos_block_device_t read_only;
    if(storage_block_ready())return 1;
    if(!native_storage_boot || !chos_nvme_init(native_storage_boot,&native_storage_acpi,&native_storage_disk))return 0;
    uint64_t sectors;if(!chos_nvme_snapshot(&native_storage_selected_identity,&sectors)||sectors!=native_storage_disk.sector_count)return 0;
    read_only=native_storage_disk;read_only.write=0;read_only.flush=0;
    native_storage_writable=0;return storage_attach_block_device(&read_only);
}
int chos_native_storage_enable_writes(void){
    if(installation_in_progress())return 0;
    uint64_t id,n;
    if(!native_storage_disk.ready || !chos_nvme_snapshot(&id,&n) || id!=native_storage_selected_identity || n!=native_storage_disk.sector_count)return 0;
    native_storage_writable=storage_attach_block_device(&native_storage_disk) && storage_write_ready();
    return native_storage_writable;
}
const char *chos_native_storage_status(void){return installation_in_progress()?"Installer owns the target until completion or cancellation.":chos_nvme_last_error();}
static chos_install_job_t installation;
static int installation_in_progress(void){return installation.state==INSTALL_WRITING||installation.state==INSTALL_FORMATTING||installation.state==INSTALL_VERIFYING;}
chos_install_job_t *chos_install_runtime(void){return &installation;}
int chos_install_choose_native(void) {
    if(installation_in_progress())return 0;
    if(!native_storage_boot){installation.state=INSTALL_FAILED;installation.error="Installation requires the UEFI installer ISO.";return 0;}
    if(native_storage_boot->flags&CHOS_BOOT_FLAG_BOOT_NVME){installation.state=INSTALL_FAILED;installation.error="Boot volume is protected. Boot the ISO to select an install target.";return 0;}
    if(!native_storage_disk.ready && !chos_nvme_init(native_storage_boot,&native_storage_acpi,&native_storage_disk)){installation.state=INSTALL_FAILED;installation.error=chos_nvme_last_error();return 0;}
    uint64_t address=native_storage_boot->installer_payload_address,bytes=native_storage_boot->installer_payload_size;
    if(!bytes||!chos_boot_info_contains_memory(native_storage_boot,address,bytes)){installation.state=INSTALL_FAILED;installation.error="Installer payload unavailable; no disk writes performed.";return 0;}
    return chos_install_select(&installation,&native_storage_disk,chos_nvme_snapshot,(const void *)(uintptr_t)address,(size_t)bytes);
}
int chos_install_start(const char *text) {
    if(!chos_install_confirm(&installation,text))return 0;
    native_storage_writable=0;storage_detach();return 1;
}
void chos_install_abort(void){chos_install_cancel(&installation);}
uint64_t installer_max_step_counter_ticks;
void chos_install_tick(void){
    chos_install_state_t previous=installation.state;
    uint64_t before=timer_counter();
    chos_install_step(&installation);
    uint64_t elapsed=timer_counter()-before;if(elapsed>installer_max_step_counter_ticks)installer_max_step_counter_ticks=elapsed;
    if(previous!=INSTALL_DONE && installation.state==INSTALL_DONE){native_storage_selected_identity=installation.plan.disk_identity;native_storage_writable=storage_attach_block_device(&native_storage_disk) && storage_write_ready();}
}
int chos_persist_display_request(uint32_t width,uint32_t height){
    if(!native_storage_writable||!storage_write_ready()||installation_in_progress())return 0;
    return chos_install_save_display(&native_storage_disk,chos_nvme_snapshot,native_storage_selected_identity,width,height);
}
int chos_native_storage_is_writable(void){return native_storage_writable&&storage_write_ready();}
int chos_native_storage_is_present(void){return native_storage_disk.ready;}
int chos_install_available(void){return native_storage_boot && !(native_storage_boot->flags&CHOS_BOOT_FLAG_BOOT_NVME) && native_storage_boot->installer_payload_size!=0u;}
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

static void console_hex_width(uint32_t value,unsigned digits){
    static const char hex[]="0123456789abcdef";
    char buffer[9];if(digits>8)digits=8;
    for(unsigned i=0;i<digits;i++)buffer[i]=hex[(value>>(4*(digits-i-1)))&15];
    buffer[digits]=0;uefi_console_write(buffer);
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
    uart_puts("CHOS ARM64 kernel 0.30.0\n[BUILD] " CHOS_BUILD_REVISION "\n");
}

static __attribute__((noreturn)) void kernel_halt_safely(void) {
    for (;;) __asm__ volatile("wfe");
}

static void uefi_desktop_task(void *argument) {
    int input_ready=*(const int *)argument;
    shell_run_scheduled(&boot_device_tree);
    for(;;){
        chos_platform_drivers_poll();chos_net_stack_poll();chos_net_manager_poll();
        if(input_ready)chos_input_poll();else chos_gui_pump();
        (void)process_service_pending_spawn_request();chos_threads_service();
        process_reap_pending();
        scheduler_sleep_ticks(1u);
    }
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
        chos_pci_scan_summary_t pci_summary = {0};
        pmm_stats_t memory_stats;
        chos_surface_t surface;
#ifdef CHOS_ENABLE_EXPERIMENTAL_NVME
        chos_block_device_t nvme_block;
#endif
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
        (void)chos_clock_init_uefi(boot_info);
        console_ready = uefi_console_init(boot_info);
        if (console_ready) {
            uefi_console_write("[BOOT] Heap bootstrap mode explicitly initialized; runtime heap lock bypassed until startup completes.\n");
            uefi_console_write("[OK] Versioned UEFI boot-information ABI validated.\n");
            uefi_console_write("[OK] Kernel ELF loaded and relocated; kernel entry reached.\n\n");
            uefi_console_report_boot_info(boot_info);
        }

        acpi_result = chos_acpi_parse(boot_info, &acpi_info);
        native_storage_boot=boot_info;native_storage_acpi=acpi_info;
        chos_power_set_conduit(acpi_result==CHOS_ACPI_OK?acpi_info.psci_conduit:0u);
        if (acpi_result==CHOS_ACPI_OK && acpi_info.pl011_base) {
            uart_attach_pl011((uintptr_t)acpi_info.pl011_base);
            uefi_console_enable_serial();
        }
        if (console_ready) {
            uefi_console_write("[BUILD] " CHOS_BUILD_REVISION "\n");
            uefi_console_write("[CLOCK] ");uefi_console_write(chos_clock_source());uefi_console_write("\n");
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
                         pci_index < pci_summary.devices_stored;
                         ++pci_index) {
                        const chos_pci_device_t *device = &pci_devices[pci_index];
                        uefi_console_write("PCI ");
                        console_hex_width(device->segment,4); uefi_console_write(":");
                        console_hex_width(device->bus,2); uefi_console_write(":");
                        console_hex_width(device->device,2); uefi_console_write(".");
                        console_hex_width(device->function,1); uefi_console_write(" id ");
                        console_hex_width(device->vendor_id,4); uefi_console_write(":");
                        console_hex_width(device->device_id,4); uefi_console_write(" class ");
                        console_hex_width(device->class_code,2); uefi_console_write(":");
                        console_hex_width(device->subclass,2); uefi_console_write("\n");
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

        if(boot_info->rng_seed_bytes==CHOS_SEED_RNG_BYTES && boot_info->rng_algorithm>=1u && boot_info->rng_algorithm<=4u){
            int seeded=chos_seed_rng_init((uint8_t *)(uintptr_t)boot_info->rng_seed,CHOS_SEED_RNG_BYTES);
            if(console_ready)uefi_console_write(seeded?"[OK] Firmware RNG seed consumed and erased; bounded HMAC-DRBG available.\n":"[WARN] Firmware RNG seed rejected; no software entropy fallback.\n");
        }
        int firmware_mmio=uefi_fdt_has_virtio_mmio(boot_info);
        /* Build translations before starting native DMA. Unsupported metadata
         * leaves the proven MMU-off recovery desktop available. */
        if(acpi_result==CHOS_ACPI_OK){
            vmm_result=vmm_prepare_uefi(boot_info,&acpi_info,pci_devices,pci_summary.devices_stored,
                                      firmware_mmio?platform_virtio_mmio_base():0u);
            if(vmm_result==VMM_OK && vmm_enable()){
                if(console_ready)uefi_console_write("[OK] UEFI CPU isolation active; identity DMA, data cache off.\n");
            }else if(console_ready){
                uefi_console_write("[WARN] UEFI application isolation unavailable; using recovery desktop: ");
                uefi_console_write(vmm_error_string(vmm_result==VMM_OK?vmm_last_error():vmm_result));
                uefi_console_write("\n");
            }
        }
        if(firmware_mmio)chos_virtio_use_mmio(platform_virtio_mmio_base());
        chos_platform_drivers_discover(boot_info,&acpi_info,pci_devices,pci_summary.devices_stored);
        chos_platform_drivers_probe(CHOS_DRIVER_TRANSPORT);
        if(console_ready) {
            uefi_console_write("[PCI] VirtIO transports: ");uefi_console_write_u64(chos_virtio_pci_count());
            uefi_console_write("; ");uefi_console_write(chos_virtio_transport_error());uefi_console_write("\n");
        }
        if(chos_virtio_aperture()) {
            if(!(boot_info->flags&CHOS_BOOT_FLAG_BOOT_NVME) && storage_init()) {
                if(console_ready)uefi_console_write(storage_mounted()?"[OK] UEFI VirtIO/CHFS mounted.\n":"[INFO] UEFI VirtIO block present; filesystem left unchanged.\n");
            } else if(console_ready && !(boot_info->flags&CHOS_BOOT_FLAG_BOOT_NVME)) {uefi_console_write("[INFO] UEFI block unavailable: ");uefi_console_write(storage_error());uefi_console_write("\n");}
        }
        if(chos_net_device_init()) {
            chos_net_stack_init();
            if(console_ready){uefi_console_write("[OK] UEFI Ethernet ready: ");uefi_console_write(chos_net_device_name());uefi_console_write("\n");}
        } else if(console_ready){uefi_console_write("[INFO] Ethernet offline: ");uefi_console_write(chos_net_device_error());uefi_console_write("\n");}
        if(boot_info->flags&CHOS_BOOT_FLAG_BOOT_NVME){
            int found=chos_probe_native_storage();
            int writable=found && storage_mounted() && chos_native_storage_enable_writes();
            if(console_ready)uefi_console_write(writable?"[OK] Installed NVMe boot volume mounted writable.\n":found&&storage_mounted()?"[WARN] Installed volume is read only; write validation failed.\n":"[WARN] NVMe boot volume could not be mounted; ROMFS recovery remains available.\n");
        }
#ifdef CHOS_ENABLE_EXPERIMENTAL_NVME
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

#else
        if (console_ready) uefi_console_write("[INFO] Experimental NVMe probing disabled by default.\n");
#endif

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
        if (uefi_fdt_has_virtio_mmio(boot_info) || chos_virtio_aperture()) {
            input_ready = chos_input_init() > 0;
            if (console_ready) {
                if (input_ready) uefi_console_write("[OK] Discovered VirtIO input queues initialized.\n");
                else {
                    uefi_console_write("[INFO] No usable VirtIO input queues: ");
                    uefi_console_write(chos_input_last_error()); uefi_console_write("\n");
                }
                if (chos_entropy_init()) uefi_console_write("[OK] Entropy initialized; nonblocking secure-source service is available.\n");
                else { uefi_console_write("[ .. ] VirtIO RNG unavailable: "); uefi_console_write(chos_entropy_last_error()); uefi_console_write("\n"); }
            } else {
                (void)chos_entropy_init();
            }
        } else if (console_ready) {
            uefi_console_write("[INFO] No usable VirtIO transport; native PCI USB discovery follows.\n");
        }
        if(console_ready)chos_xhci_set_logger(uefi_console_write);
        chos_platform_drivers_probe(CHOS_DRIVER_INPUT);
        if(console_ready) {
            uefi_console_write(chos_xhci_running()?"[OK] xHCI controller running: ":"[INFO] xHCI unavailable: ");
            uefi_console_write(chos_xhci_error());uefi_console_write("; HID devices=");
            uefi_console_write_u64(chos_xhci_device_count());uefi_console_write("\n");
        }
        chos_gui_set_driver_report(chos_platform_driver_report,chos_platform_needs_input_recovery);
        chos_net_manager_init();
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
        if (gic_timer_ready) {
            /* Do not WFI on a timer that only initialized registers. Prove that
             * IRQ delivery advances ticks first, using the independent counter
             * and an iteration cap so a broken GIC cannot trap the splash. */
            uint64_t ticks=timer_ticks(), start=timer_counter();
            uint64_t budget=timer_frequency_hz()/5u;
            unsigned polls;
            for(polls=0;polls<2000000u && timer_ticks()-ticks<2u;++polls) {
                chos_platform_drivers_poll();chos_gui_pump();
                if(!budget || timer_counter()-start>=budget) break;
                __asm__ volatile("yield" ::: "memory");
            }
            if(timer_ticks()-ticks<2u) {
                interrupt_disable_irqs();timer_stop();gic_timer_ready=0;
                if(console_ready) uefi_console_write("[WARN] Timer IRQ delivery did not progress; using polled desktop (no WFI).\n");
            } else if(console_ready) {
                uefi_console_write("[OK] Timer IRQ delivery verified before low-power idle.\n");
            }
        }
        uefi_console_set_graphics_logging(0);
        if(gic_timer_ready && vmm_is_enabled()){
            uint32_t desktop_id=0;
            if(scheduler_set_preemption_enabled(1) &&
               scheduler_create_task("uefi-desktop",uefi_desktop_task,&input_ready,&desktop_id)){
                if(console_ready)uefi_console_write("[OK] Scheduled UEFI desktop and isolated application runtime ready.\n");
                (void)scheduler_run();
                if(console_ready)uefi_console_write("[FAIL] UEFI scheduler stopped; recovery desktop resumed.\n");
            }
        }
        for (;;) {
            chos_platform_drivers_poll();chos_net_stack_poll();chos_net_manager_poll();
            if (input_ready) chos_input_poll();
            else chos_gui_pump();
            if (gic_timer_ready) __asm__ volatile("wfi" ::: "memory");
            else __asm__ volatile("yield" ::: "memory");
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
        chos_power_set_conduit(boot_device_tree.psci_conduit);
        for(size_t i=0;i<boot_device_tree.device_count;++i) {
            const fdt_device_t *d=&boot_device_tree.devices[i];
            static const char pl031[]="arm,pl031";unsigned k=0;
            while(k<sizeof(pl031) && d->compatible[k]==pl031[k])++k;
            if(k==sizeof(pl031) && d->has_reg && d->reg_size>=4)(void)chos_clock_use_pl031((uintptr_t)d->reg_base);
        }
        if(fdt_info_has_qemu_virtio_mmio(&boot_device_tree))chos_virtio_use_mmio(platform_virtio_mmio_base());
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

    if (chos_net_device_init()) {
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
        uart_puts("[ OK ] Non-blocking DHCP, DNS A, ARP/ICMP and bounded TCP clients initialized; TLS remains unavailable\n");
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
    chos_net_manager_init();
    chos_gui_startup_complete();

    uart_puts("\n");
    shell_run(&boot_device_tree);
}
