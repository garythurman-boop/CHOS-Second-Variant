#include <stddef.h>
#include <stdint.h>
#include "chos/nvme.h"
#include "chos/pci.h"
#include "chos/pmm.h"

#define NVME_PCI_CLASS 0x01u
#define NVME_PCI_SUBCLASS 0x08u
#define NVME_PCI_PROGIF 0x02u
#define NVME_PCI_VENDOR_VMWARE 0x15adu
#define NVME_PCI_DEVICE_VMWARE 0x07f0u
#define NVME_MMIO_MIN_BYTES 0x2000u
#define NVME_PAGE_SIZE 4096u
#define NVME_QUEUE_DEPTH 16u
#define NVME_WAIT_SPINS 50000000u
#define NVME_DOORBELL_BASE 0x1000u
#define NVME_IDENTIFY_CONTROLLER 0x01u
#define NVME_IDENTIFY_NAMESPACE 0x00u
#define NVME_ADMIN_CREATE_IO_SQ 0x01u
#define NVME_ADMIN_CREATE_IO_CQ 0x05u
#define NVME_OP_FLUSH 0x00u
#define NVME_OP_WRITE 0x01u
#define NVME_OP_READ 0x02u
#define NVME_OP_IDENTIFY 0x06u
#define NVME_CC_EN (1u << 0u)
#define NVME_CSTS_RDY (1u << 0u)
#define NVME_CSTS_CFS (1u << 1u)
#define NVME_PCI_COMMAND_MEMORY (1u << 1u)
#define NVME_PCI_COMMAND_MASTER (1u << 2u)
#define NVME_IDENTIFY_BYTES 4096u
#define NVME_MAX_PAGE_QUEUE_DEPTH 32u

enum {
    REG_CAP = 0x0000, REG_VS = 0x0008, REG_INTMS = 0x000c,
    REG_CC = 0x0014, REG_CSTS = 0x001c, REG_NSSR = 0x0020,
    REG_AQA = 0x0024, REG_ASQ = 0x0028, REG_ACQ = 0x0030
};

typedef struct __attribute__((packed)) {
    uint32_t dword0;
    uint32_t namespace_id;
    uint64_t reserved;
    uint64_t metadata_pointer;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

typedef struct __attribute__((packed)) {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} nvme_completion_t;

_Static_assert(sizeof(nvme_command_t) == 64u, "NVMe command must be 64 bytes");
_Static_assert(sizeof(nvme_completion_t) == 16u, "NVMe completion must be 16 bytes");

typedef struct {
    volatile uint8_t *mmio;
    uint64_t bar_base;
    uint64_t admin_sq_phys, admin_cq_phys;
    uint64_t io_sq_phys, io_cq_phys;
    uint64_t identify_phys, namespace_phys, bounce_phys;
    volatile nvme_command_t *admin_sq;
    volatile nvme_completion_t *admin_cq;
    volatile nvme_command_t *io_sq;
    volatile nvme_completion_t *io_cq;
    uint8_t *identify;
    uint8_t *namespace_identify;
    uint8_t *bounce;
    uint16_t queue_depth;
    uint16_t admin_sq_tail, admin_cq_head, admin_cid;
    uint16_t io_sq_tail, io_cq_head, io_cid;
    uint8_t admin_phase, io_phase;
    uint8_t doorbell_stride_log2;
    uint64_t sector_count;
    int ready;
    int flush_supported;
} nvme_state_t;

static nvme_state_t nvme;
static const char *last_error = "NVMe probe not run";

static void bytes_zero(void *ptr, size_t length) {
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0u; i < length; ++i) p[i] = 0u;
}
static void bytes_copy(void *dst, const void *src, size_t length) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; size_t i;
    for (i = 0u; i < length; ++i) d[i] = s[i];
}
static uint32_t mmio_read32(uint32_t offset) { return *(volatile uint32_t *)(uintptr_t)(nvme.bar_base + offset); }
static void mmio_write32(uint32_t offset, uint32_t value) { *(volatile uint32_t *)(uintptr_t)(nvme.bar_base + offset) = value; }
static uint64_t mmio_read64(uint32_t offset) {
    uint32_t low = mmio_read32(offset), high = mmio_read32(offset + 4u);
    return (uint64_t)low | ((uint64_t)high << 32u);
}
static void mmio_write64(uint32_t offset, uint64_t value) {
    mmio_write32(offset + 4u, (uint32_t)(value >> 32u));
    mmio_write32(offset, (uint32_t)value);
    __asm__ volatile("dsb sy" ::: "memory");
}
static void full_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static size_t cache_line_size(void) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    return (size_t)4u << ((ctr >> 16u) & 0x0fu);
}
static void cache_clean(const void *ptr, size_t length) {
    uintptr_t start = (uintptr_t)ptr, end = start + length, p;
    size_t line = cache_line_size();
    if (length == 0u || end < start || line == 0u) return;
    start &= ~((uintptr_t)line - 1u); end = (end + line - 1u) & ~((uintptr_t)line - 1u);
    for (p = start; p < end; p += line) __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb osh" ::: "memory");
}
static void cache_clean_invalidate(void *ptr, size_t length) {
    uintptr_t start = (uintptr_t)ptr, end = start + length, p;
    size_t line = cache_line_size();
    if (length == 0u || end < start || line == 0u) return;
    start &= ~((uintptr_t)line - 1u); end = (end + line - 1u) & ~((uintptr_t)line - 1u);
    for (p = start; p < end; p += line) __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb osh" ::: "memory");
}
static void cache_invalidate(void *ptr, size_t length) {
    uintptr_t start = (uintptr_t)ptr, end = start + length, p;
    size_t line = cache_line_size();
    if (length == 0u || end < start || line == 0u) return;
    start &= ~((uintptr_t)line - 1u); end = (end + line - 1u) & ~((uintptr_t)line - 1u);
    for (p = start; p < end; p += line) __asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb osh" ::: "memory");
}

static int allocate_page(uint64_t *physical, void **virtual_address) {
    if (!pmm_alloc_pages(1u, physical) || *physical == 0u) return 0;
    *virtual_address = (void *)(uintptr_t)*physical;
    bytes_zero(*virtual_address, NVME_PAGE_SIZE);
    cache_clean(*virtual_address, NVME_PAGE_SIZE);
    return 1;
}

static int wait_csts(uint32_t mask, uint32_t expected) {
    uint32_t spin;
    for (spin = 0u; spin < NVME_WAIT_SPINS; ++spin) {
        uint32_t csts = mmio_read32(REG_CSTS);
        if ((csts & NVME_CSTS_CFS) != 0u) { last_error = "controller fatal status set"; return 0; }
        if ((csts & mask) == expected) return 1;
        __asm__ volatile("yield" ::: "memory");
    }
    last_error = "timeout waiting for NVMe controller ready state";
    return 0;
}

static uint32_t doorbell_offset(uint16_t queue_id, int completion) {
    uint32_t stride = 4u << nvme.doorbell_stride_log2;
    return NVME_DOORBELL_BASE + ((uint32_t)queue_id * 2u + (completion ? 1u : 0u)) * stride;
}

static int completion_status_ok(uint16_t status) { return (status >> 1u) == 0u; }

static int submit_admin(nvme_command_t *command, uint32_t *result_out) {
    uint16_t cid = ++nvme.admin_cid;
    uint32_t spin;
    command->dword0 = (command->dword0 & 0xffffu) | ((uint32_t)cid << 16u);
    bytes_copy((void *)&nvme.admin_sq[nvme.admin_sq_tail], command, sizeof(*command));
    cache_clean((const void *)&nvme.admin_sq[nvme.admin_sq_tail], sizeof(*command));
    nvme.admin_sq_tail = (uint16_t)((nvme.admin_sq_tail + 1u) % nvme.queue_depth);
    full_barrier();
    mmio_write32(doorbell_offset(0u, 0), nvme.admin_sq_tail);
    for (spin = 0u; spin < NVME_WAIT_SPINS; ++spin) {
        nvme_completion_t c;
        volatile nvme_completion_t *entry = &nvme.admin_cq[nvme.admin_cq_head];
        cache_invalidate((void *)entry, sizeof(*entry));
        c.status = entry->status;
        if ((c.status & 1u) != nvme.admin_phase) {
            __asm__ volatile("yield" ::: "memory");
            continue;
        }
        c.result = entry->result; c.reserved = entry->reserved;
        c.sq_head = entry->sq_head; c.sq_id = entry->sq_id; c.command_id = entry->command_id;
        if (c.command_id != cid) { last_error = "unexpected NVMe admin completion ID"; return 0; }
        nvme.admin_cq_head++;
        if (nvme.admin_cq_head == nvme.queue_depth) { nvme.admin_cq_head = 0u; nvme.admin_phase ^= 1u; }
        full_barrier(); mmio_write32(doorbell_offset(0u, 1), nvme.admin_cq_head);
        if (!completion_status_ok(c.status)) { last_error = "NVMe admin command failed"; return 0; }
        if (result_out != NULL) *result_out = c.result;
        return 1;
    }
    last_error = "timeout waiting for NVMe admin completion";
    return 0;
}

static int submit_io(nvme_command_t *command) {
    uint16_t cid = ++nvme.io_cid;
    uint32_t spin;
    command->dword0 = (command->dword0 & 0xffffu) | ((uint32_t)cid << 16u);
    bytes_copy((void *)&nvme.io_sq[nvme.io_sq_tail], command, sizeof(*command));
    cache_clean((const void *)&nvme.io_sq[nvme.io_sq_tail], sizeof(*command));
    nvme.io_sq_tail = (uint16_t)((nvme.io_sq_tail + 1u) % nvme.queue_depth);
    full_barrier(); mmio_write32(doorbell_offset(1u, 0), nvme.io_sq_tail);
    for (spin = 0u; spin < NVME_WAIT_SPINS; ++spin) {
        nvme_completion_t c;
        volatile nvme_completion_t *entry = &nvme.io_cq[nvme.io_cq_head];
        cache_invalidate((void *)entry, sizeof(*entry));
        c.status = entry->status;
        if ((c.status & 1u) != nvme.io_phase) { __asm__ volatile("yield" ::: "memory"); continue; }
        c.result = entry->result; c.reserved = entry->reserved;
        c.sq_head = entry->sq_head; c.sq_id = entry->sq_id; c.command_id = entry->command_id;
        if (c.command_id != cid) { last_error = "unexpected NVMe I/O completion ID"; return 0; }
        nvme.io_cq_head++;
        if (nvme.io_cq_head == nvme.queue_depth) { nvme.io_cq_head = 0u; nvme.io_phase ^= 1u; }
        full_barrier(); mmio_write32(doorbell_offset(1u, 1), nvme.io_cq_head);
        if (!completion_status_ok(c.status)) { last_error = "NVMe I/O command failed"; return 0; }
        return 1;
    }
    last_error = "timeout waiting for NVMe I/O completion";
    return 0;
}

static void command_init(nvme_command_t *command, uint8_t opcode, uint32_t nsid) {
    bytes_zero(command, sizeof(*command));
    command->dword0 = opcode;
    command->namespace_id = nsid;
}

static int admin_identify(uint32_t nsid, uint32_t cns, uint64_t buffer_phys, void *buffer) {
    nvme_command_t command;
    command_init(&command, NVME_OP_IDENTIFY, nsid);
    command.prp1 = buffer_phys;
    command.cdw10 = cns;
    cache_clean_invalidate(buffer, NVME_IDENTIFY_BYTES);
    if (!submit_admin(&command, NULL)) return 0;
    cache_invalidate(buffer, NVME_IDENTIFY_BYTES);
    return 1;
}

static void release_controller(void) {
    /* Keep DMA pages allocated on failures; a timed-out controller might still
     * own them. Disable it only when the status register is safe to access. */
    nvme.ready = 0;
}

static int nvme_read(chos_block_device_t *device, uint64_t first_sector,
                     uint32_t sector_count, void *buffer) {
    uint8_t *dst = (uint8_t *)buffer;
    (void)device;
    if (!nvme.ready || buffer == NULL || sector_count == 0u ||
        first_sector >= nvme.sector_count || sector_count > nvme.sector_count - first_sector) return 0;
    while (sector_count != 0u) {
        uint32_t count = sector_count > CHOS_NVME_MAX_TRANSFER_SECTORS ? CHOS_NVME_MAX_TRANSFER_SECTORS : sector_count;
        nvme_command_t command;
        command_init(&command, NVME_OP_READ, 1u);
        command.prp1 = nvme.bounce_phys;
        command.cdw10 = (uint32_t)first_sector;
        command.cdw11 = (uint32_t)(first_sector >> 32u);
        command.cdw12 = count - 1u;
        cache_clean_invalidate(nvme.bounce, (size_t)count * CHOS_NVME_SECTOR_SIZE);
        if (!submit_io(&command)) { release_controller(); return 0; }
        cache_invalidate(nvme.bounce, (size_t)count * CHOS_NVME_SECTOR_SIZE);
        bytes_copy(dst, nvme.bounce, (size_t)count * CHOS_NVME_SECTOR_SIZE);
        dst += (size_t)count * CHOS_NVME_SECTOR_SIZE;
        first_sector += count; sector_count -= count;
    }
    return 1;
}

static int nvme_write(chos_block_device_t *device, uint64_t first_sector,
                      uint32_t sector_count, const void *buffer) {
    const uint8_t *src = (const uint8_t *)buffer;
    (void)device;
    if (!nvme.ready || buffer == NULL || sector_count == 0u ||
        first_sector >= nvme.sector_count || sector_count > nvme.sector_count - first_sector) return 0;
    while (sector_count != 0u) {
        uint32_t count = sector_count > CHOS_NVME_MAX_TRANSFER_SECTORS ? CHOS_NVME_MAX_TRANSFER_SECTORS : sector_count;
        nvme_command_t command;
        bytes_copy(nvme.bounce, src, (size_t)count * CHOS_NVME_SECTOR_SIZE);
        cache_clean(nvme.bounce, (size_t)count * CHOS_NVME_SECTOR_SIZE);
        command_init(&command, NVME_OP_WRITE, 1u);
        command.prp1 = nvme.bounce_phys;
        command.cdw10 = (uint32_t)first_sector;
        command.cdw11 = (uint32_t)(first_sector >> 32u);
        command.cdw12 = count - 1u;
        if (!submit_io(&command)) { release_controller(); return 0; }
        src += (size_t)count * CHOS_NVME_SECTOR_SIZE;
        first_sector += count; sector_count -= count;
    }
    return 1;
}

static int nvme_flush(chos_block_device_t *device) {
    nvme_command_t command;
    (void)device;
    if (!nvme.ready) return 0;
    command_init(&command, NVME_OP_FLUSH, 1u);
    return submit_io(&command);
}

static int find_nvme(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                     chos_pci_device_t *out) {
    chos_pci_device_t devices[CHOS_PCI_MAX_DEVICES];
    chos_pci_scan_summary_t summary;
    uint32_t i;
    if (!chos_pci_enumerate(boot, acpi, devices, CHOS_PCI_MAX_DEVICES, &summary)) return 0;
    for (i = 0u; i < summary.devices_stored; ++i) {
        const chos_pci_device_t *d = &devices[i];
        if (d->class_code == NVME_PCI_CLASS && d->subclass == NVME_PCI_SUBCLASS &&
            d->programming_interface == NVME_PCI_PROGIF) { *out = *d; return 1; }
    }
    return 0;
}

int chos_nvme_init(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                   chos_block_device_t *device_out) {
    chos_pci_device_t device;
    chos_nvme_capabilities_t caps;
    uint64_t cap, bar_address, required_mmio;
    uint32_t spin, ready;
    uint32_t identify_nn;
    uint16_t queue_depth;
    void *mem;
    if (device_out == NULL || !chos_boot_info_validate(boot) || acpi == NULL || acpi->valid == 0u) {
        last_error = "invalid NVMe boot/platform information"; return 0;
    }
    bytes_zero(&nvme, sizeof(nvme)); bytes_zero(device_out, sizeof(*device_out));
    if (!find_nvme(boot, acpi, &device)) { last_error = "no PCI NVMe controller found"; return 0; }
    if (!chos_pci_decode_memory_bar(&device, 0u, &bar_address, NULL)) {
        last_error = "NVMe BAR0 is not an assigned memory BAR"; return 0;
    }
    if ((bar_address & (NVME_PAGE_SIZE - 1u)) != 0u ||
        !chos_boot_info_contains_mmio(boot, bar_address, NVME_MMIO_MIN_BYTES)) {
        last_error = "NVMe BAR0 is unaligned or not covered by EFI MMIO reservations"; return 0;
    }
    if (!chos_pci_enable_memory_busmaster(boot, acpi, &device)) {
        last_error = "could not enable PCI memory decoding and bus mastering"; return 0;
    }
    nvme.bar_base = bar_address; nvme.mmio = (volatile uint8_t *)(uintptr_t)bar_address;
    cap = mmio_read64(REG_CAP);
    if (!chos_nvme_parse_capabilities(cap, &caps)) {
        last_error = "unsupported NVMe CAP (page size, queue depth, or NVM command set)"; return 0;
    }
    nvme.doorbell_stride_log2 = caps.doorbell_stride_log2;
    required_mmio = NVME_DOORBELL_BASE + (uint64_t)3u * (UINT64_C(4) << caps.doorbell_stride_log2) + 4u;
    if (!chos_boot_info_contains_mmio(boot, bar_address, required_mmio)) {
        last_error = "NVMe BAR reservation is too small for queue doorbells"; return 0;
    }
    queue_depth = caps.maximum_queue_entries < NVME_QUEUE_DEPTH ? caps.maximum_queue_entries : NVME_QUEUE_DEPTH;
    if (queue_depth < 8u) { last_error = "NVMe controller advertises queues that are too small"; return 0; }
    nvme.queue_depth = queue_depth;
    mmio_write32(REG_INTMS, UINT32_MAX); /* polled mode: mask all NVMe interrupts */
    if ((mmio_read32(REG_CC) & NVME_CC_EN) != 0u) {
        mmio_write32(REG_CC, mmio_read32(REG_CC) & ~NVME_CC_EN);
        if (!wait_csts(NVME_CSTS_RDY, 0u)) return 0;
    } else if (!wait_csts(NVME_CSTS_RDY, 0u)) {
        /* Some firmware leaves the controller unready; continue only if RDY=0. */
        if ((mmio_read32(REG_CSTS) & NVME_CSTS_RDY) != 0u) return 0;
    }
    if (!allocate_page(&nvme.admin_sq_phys, &mem)) { last_error = "no PMM page for NVMe admin SQ"; return 0; } nvme.admin_sq = (volatile nvme_command_t *)mem;
    if (!allocate_page(&nvme.admin_cq_phys, &mem)) { last_error = "no PMM page for NVMe admin CQ"; return 0; } nvme.admin_cq = (volatile nvme_completion_t *)mem;
    if (!allocate_page(&nvme.io_sq_phys, &mem)) { last_error = "no PMM page for NVMe I/O SQ"; return 0; } nvme.io_sq = (volatile nvme_command_t *)mem;
    if (!allocate_page(&nvme.io_cq_phys, &mem)) { last_error = "no PMM page for NVMe I/O CQ"; return 0; } nvme.io_cq = (volatile nvme_completion_t *)mem;
    if (!allocate_page(&nvme.identify_phys, &mem)) { last_error = "no PMM page for NVMe identify data"; return 0; } nvme.identify = (uint8_t *)mem;
    if (!allocate_page(&nvme.namespace_phys, &mem)) { last_error = "no PMM page for NVMe namespace data"; return 0; } nvme.namespace_identify = (uint8_t *)mem;
    if (!allocate_page(&nvme.bounce_phys, &mem)) { last_error = "no PMM page for NVMe data buffer"; return 0; } nvme.bounce = (uint8_t *)mem;
    nvme.admin_phase = 1u; nvme.io_phase = 1u;
    nvme.admin_sq_tail = nvme.admin_cq_head = 0u; nvme.admin_cid = 0u;
    nvme.io_sq_tail = nvme.io_cq_head = 0u; nvme.io_cid = 0u;

    mmio_write32(REG_AQA, ((uint32_t)(queue_depth - 1u) << 16u) | (uint32_t)(queue_depth - 1u));
    mmio_write64(REG_ASQ, nvme.admin_sq_phys);
    mmio_write64(REG_ACQ, nvme.admin_cq_phys);
    /* IOSQES=64 bytes, IOCQES=16 bytes, page size exponent 0 (4 KiB). */
    mmio_write32(REG_CC, NVME_CC_EN | (6u << 16u) | (4u << 20u));
    full_barrier();
    if (!wait_csts(NVME_CSTS_RDY, NVME_CSTS_RDY)) return 0;

    bytes_zero(nvme.identify, NVME_IDENTIFY_BYTES);
    if (!admin_identify(0u, NVME_IDENTIFY_CONTROLLER, nvme.identify_phys, nvme.identify)) return 0;
    identify_nn = *(const uint32_t *)(const void *)(nvme.identify + 516u);
    if (identify_nn == 0u) { last_error = "NVMe controller reports no namespaces"; return 0; }
    bytes_zero(nvme.namespace_identify, NVME_IDENTIFY_BYTES);
    if (!admin_identify(1u, NVME_IDENTIFY_NAMESPACE, nvme.namespace_phys, nvme.namespace_identify)) return 0;
    if (!chos_nvme_parse_namespace_identify(nvme.namespace_identify, NVME_IDENTIFY_BYTES, &nvme.sector_count)) {
        last_error = "NVMe namespace 1 is absent or not a plain 512-byte logical-block namespace"; return 0;
    }
    if (nvme.sector_count > (UINT64_MAX / CHOS_NVME_SECTOR_SIZE)) {
        last_error = "NVMe namespace size overflows byte capacity"; return 0;
    }
    /* Create a single polled I/O completion queue then submission queue. */
    {
        nvme_command_t command;
        command_init(&command, NVME_ADMIN_CREATE_IO_CQ, 0u);
        command.prp1 = nvme.io_cq_phys;
        command.cdw10 = ((uint32_t)(queue_depth - 1u) << 16u) | 1u;
        command.cdw11 = 1u; /* physically contiguous, interrupts disabled */
        if (!submit_admin(&command, NULL)) return 0;
        command_init(&command, NVME_ADMIN_CREATE_IO_SQ, 0u);
        command.prp1 = nvme.io_sq_phys;
        command.cdw10 = ((uint32_t)(queue_depth - 1u) << 16u) | 1u;
        command.cdw11 = (1u << 16u) | 1u; /* CQID=1, physically contiguous */
        if (!submit_admin(&command, NULL)) return 0;
    }
    nvme.ready = 1;
    device_out->name = "nvme0n1";
    device_out->sector_count = nvme.sector_count;
    device_out->context = &nvme;
    device_out->read = nvme_read;
    device_out->write = nvme_write;
    device_out->flush = nvme_flush;
    device_out->ready = 1;
    last_error = "ok (polled PCI NVMe, namespace 1, 512-byte logical sectors)";
    /* Touch the status once after queue setup so a fatal status is reported early. */
    ready = mmio_read32(REG_CSTS);
    if ((ready & NVME_CSTS_CFS) != 0u) {
        nvme.ready = 0; device_out->ready = 0; last_error = "NVMe fatal status after I/O queue setup"; return 0;
    }
    for (spin = 0u; spin < 1u; ++spin) __asm__ volatile("nop");
    return 1;
}

const char *chos_nvme_last_error(void) { return last_error; }
int chos_nvme_ready(void) { return nvme.ready; }
