#include <stddef.h>
#include <stdint.h>
#include "chos/blockdev.h"
#include "chos/platform.h"
#include "chos/virtio_blk.h"

#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_DEVICE_BLOCK 2u
#define VIRTIO_F_VERSION_1 (1u << 0)
#define VIRTIO_BLK_F_FLUSH (1u << 9)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u
#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u
#define VIRTIO_BLK_T_IN 0u
#define VIRTIO_BLK_T_OUT 1u
#define VIRTIO_BLK_T_FLUSH 4u
#define VQ_SIZE 8u
#define WAIT_LIMIT 20000000u
#define VIRTIO_MMIO_SLOT_SIZE 0x200u
#define VIRTIO_MMIO_SLOT_COUNT 32u

enum {
    REG_MAGIC=0x000, REG_VERSION=0x004, REG_DEVICE_ID=0x008, REG_VENDOR_ID=0x00c,
    REG_DEVICE_FEATURES=0x010, REG_DEVICE_FEATURES_SEL=0x014,
    REG_DRIVER_FEATURES=0x020, REG_DRIVER_FEATURES_SEL=0x024,
    REG_QUEUE_SEL=0x030, REG_QUEUE_NUM_MAX=0x034, REG_QUEUE_NUM=0x038,
    REG_QUEUE_READY=0x044, REG_QUEUE_NOTIFY=0x050,
    REG_INTERRUPT_STATUS=0x060, REG_INTERRUPT_ACK=0x064, REG_STATUS=0x070,
    REG_QUEUE_DESC_LOW=0x080, REG_QUEUE_DESC_HIGH=0x084,
    REG_QUEUE_DRIVER_LOW=0x090, REG_QUEUE_DRIVER_HIGH=0x094,
    REG_QUEUE_DEVICE_LOW=0x0a0, REG_QUEUE_DEVICE_HIGH=0x0a4,
    REG_CONFIG_CAPACITY_LOW=0x100, REG_CONFIG_CAPACITY_HIGH=0x104
};

typedef struct { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; } __attribute__((packed)) virtq_desc_t;
typedef struct { uint32_t type; uint32_t reserved; uint64_t sector; } __attribute__((packed)) virtio_blk_req_t;
typedef struct { uint32_t id; uint32_t len; } __attribute__((packed)) virtq_used_elem_t;
typedef struct {
    volatile uint16_t flags; volatile uint16_t idx; volatile uint16_t ring[VQ_SIZE];
    volatile uint16_t used_event;
} __attribute__((aligned(2))) virtq_avail_t;
typedef struct {
    volatile uint16_t flags; volatile uint16_t idx; volatile virtq_used_elem_t ring[VQ_SIZE];
    volatile uint16_t avail_event;
} __attribute__((aligned(4))) virtq_used_t;

static virtq_desc_t queue_desc[VQ_SIZE] __attribute__((aligned(16)));
static virtq_avail_t queue_avail __attribute__((aligned(2)));
static virtq_used_t queue_used __attribute__((aligned(4)));
static virtio_blk_req_t request_header __attribute__((aligned(16)));
static volatile uint8_t request_status;
static uint16_t queue_size;
static uint16_t available_idx;
static uint16_t last_used_idx;
static int driver_ready;
static int flush_supported;
static uintptr_t mmio_base;
static uintptr_t detected_mmio_base;
static const char *last_error = "probe not run";

const char *virtio_blk_last_error(void) { return last_error; }
uintptr_t virtio_blk_detected_base(void) { return detected_mmio_base; }

static volatile uint32_t *reg32(uint32_t offset) { return (volatile uint32_t *)(mmio_base + offset); }
static uint32_t read_reg(uint32_t offset) { return *reg32(offset); }
static void write_reg(uint32_t offset, uint32_t value) { *reg32(offset) = value; }
static void dma_store_barrier(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void dma_load_barrier(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static void cpu_full_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static uint64_t join_u64(uint32_t low, uint32_t high) { return (uint64_t)low | ((uint64_t)high << 32); }
static void set_status(uint32_t value) { write_reg(REG_STATUS, value); cpu_full_barrier(); }

static int virtio_request(uint32_t type, uint64_t sector, uint32_t count, void *buffer, int guest_writes) {
    uint16_t used_before; uint32_t spin; uint64_t buffer_address = (uint64_t)(uintptr_t)buffer;
    if (!driver_ready) return 0;
    if ((type == VIRTIO_BLK_T_IN || type == VIRTIO_BLK_T_OUT) &&
        (count == 0u || buffer == NULL || count > UINT32_MAX / CHOS_BLOCK_SECTOR_SIZE)) return 0;
    if (type == VIRTIO_BLK_T_FLUSH && !flush_supported) return 1;
    request_header.type = type; request_header.reserved = 0u; request_header.sector = sector;
    request_status = 0xffu;
    queue_desc[0].addr = (uint64_t)(uintptr_t)&request_header; queue_desc[0].len = sizeof(request_header);
    queue_desc[0].flags = VIRTQ_DESC_F_NEXT; queue_desc[0].next = (type == VIRTIO_BLK_T_FLUSH) ? 2u : 1u;
    if (type != VIRTIO_BLK_T_FLUSH) {
        queue_desc[1].addr = buffer_address; queue_desc[1].len = count * CHOS_BLOCK_SECTOR_SIZE;
        queue_desc[1].flags = VIRTQ_DESC_F_NEXT | (guest_writes ? 0u : VIRTQ_DESC_F_WRITE); queue_desc[1].next = 2u;
    }
    queue_desc[2].addr = (uint64_t)(uintptr_t)&request_status; queue_desc[2].len = 1u;
    queue_desc[2].flags = VIRTQ_DESC_F_WRITE; queue_desc[2].next = 0u;
    used_before = queue_used.idx;
    queue_avail.ring[available_idx & (queue_size - 1u)] = 0u;
    dma_store_barrier(); ++available_idx; queue_avail.idx = available_idx; dma_store_barrier();
    write_reg(REG_QUEUE_NOTIFY, 0u);
    for (spin = 0u; spin < WAIT_LIMIT; ++spin) {
        dma_load_barrier();
        if (queue_used.idx != used_before) break;
    }
    if (spin == WAIT_LIMIT) {
        /* A missing completion makes queue reuse unsafe. Quarantine the driver. */
        driver_ready = 0;
        return 0;
    }
    last_used_idx = queue_used.idx;
    (void)last_used_idx;
    dma_load_barrier();
    return request_status == 0u;
}

static int block_read_cb(chos_block_device_t *device, uint64_t first, uint32_t count, void *buffer) {
    (void)device; return virtio_request(VIRTIO_BLK_T_IN, first, count, buffer, 0);
}
static int block_write_cb(chos_block_device_t *device, uint64_t first, uint32_t count, const void *buffer) {
    (void)device; return virtio_request(VIRTIO_BLK_T_OUT, first, count, (void *)buffer, 1);
}
static int block_flush_cb(chos_block_device_t *device) { (void)device; return virtio_request(VIRTIO_BLK_T_FLUSH, 0u, 0u, NULL, 1); }

int virtio_blk_init(chos_block_device_t *out) {
    uint32_t status, max_queue, features0, features1, driver_features0 = 0u;
    uint32_t capacity_low, capacity_high; uint16_t q;
    uintptr_t first_base; uint32_t slot; int found = 0; int saw_legacy_block = 0;
    if (out == NULL) { last_error = "invalid output block-device pointer"; return 0; }
    out->ready = 0; out->sector_count = 0u;
    driver_ready = 0; flush_supported = 0; detected_mmio_base = 0u;
    first_base = platform_virtio_mmio_base();
    if (first_base == 0u) { last_error = "platform has no VirtIO-MMIO aperture"; return 0; }

    /* QEMU virt allocates one 0x200-byte MMIO transport slot per device.
       The device's slot assignment is not a stable ABI, so scan the aperture
       instead of assuming the block transport is always at slot zero. */
    for (slot = 0u; slot < VIRTIO_MMIO_SLOT_COUNT; ++slot) {
        uintptr_t candidate = first_base + (uintptr_t)slot * VIRTIO_MMIO_SLOT_SIZE;
        mmio_base = candidate;
        if (read_reg(REG_MAGIC) != VIRTIO_MAGIC || read_reg(REG_DEVICE_ID) != VIRTIO_DEVICE_BLOCK) continue;
        if (read_reg(REG_VERSION) < 2u) { saw_legacy_block = 1; continue; }
        detected_mmio_base = candidate; found = 1; break;
    }
    if (!found) {
        mmio_base = 0u;
        last_error = saw_legacy_block ? "VirtIO block found, but only legacy MMIO transport is supported" : "no VirtIO block device found in 32 MMIO slots";
        return 0;
    }

    set_status(0u); set_status(VIRTIO_STATUS_ACKNOWLEDGE); set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    write_reg(REG_DEVICE_FEATURES_SEL, 0u); features0 = read_reg(REG_DEVICE_FEATURES);
    write_reg(REG_DEVICE_FEATURES_SEL, 1u); features1 = read_reg(REG_DEVICE_FEATURES);
    if ((features1 & VIRTIO_F_VERSION_1) == 0u) { set_status(VIRTIO_STATUS_FAILED); last_error = "device does not offer VIRTIO_F_VERSION_1"; return 0; }
    if ((features0 & VIRTIO_BLK_F_FLUSH) != 0u) { driver_features0 |= VIRTIO_BLK_F_FLUSH; flush_supported = 1; }
    write_reg(REG_DRIVER_FEATURES_SEL, 0u); write_reg(REG_DRIVER_FEATURES, driver_features0);
    write_reg(REG_DRIVER_FEATURES_SEL, 1u); write_reg(REG_DRIVER_FEATURES, VIRTIO_F_VERSION_1);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    set_status(status);
    if ((read_reg(REG_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) { set_status(VIRTIO_STATUS_FAILED); last_error = "device rejected negotiated features"; return 0; }
    write_reg(REG_QUEUE_SEL, 0u); max_queue = read_reg(REG_QUEUE_NUM_MAX);
    if (max_queue < 4u) { set_status(VIRTIO_STATUS_FAILED); last_error = "device request queue is too small"; return 0; }
    queue_size = VQ_SIZE; while (queue_size > max_queue) queue_size >>= 1;
    for (q = 0u; q < VQ_SIZE; ++q) { queue_desc[q].addr=0u; queue_desc[q].len=0u; queue_desc[q].flags=0u; queue_desc[q].next=0u; queue_avail.ring[q]=0u; queue_used.ring[q].id=0u; queue_used.ring[q].len=0u; }
    queue_avail.flags=0u; queue_avail.idx=0u; queue_avail.used_event=0u;
    queue_used.flags=0u; queue_used.idx=0u; queue_used.avail_event=0u;
    available_idx = 0u; last_used_idx = 0u;
    write_reg(REG_QUEUE_NUM, queue_size);
    write_reg(REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)queue_desc); write_reg(REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)queue_desc >> 32));
    write_reg(REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)&queue_avail); write_reg(REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)&queue_avail >> 32));
    write_reg(REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)&queue_used); write_reg(REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)&queue_used >> 32));
    write_reg(REG_QUEUE_READY, 1u); cpu_full_barrier();
    capacity_low = read_reg(REG_CONFIG_CAPACITY_LOW); capacity_high = read_reg(REG_CONFIG_CAPACITY_HIGH);
    out->sector_count = join_u64(capacity_low, capacity_high);
    if (out->sector_count < 8u) { set_status(VIRTIO_STATUS_FAILED); last_error = "device reported an invalid capacity"; return 0; }
    out->name = "virtio-blk0"; out->context = NULL; out->read = block_read_cb; out->write = block_write_cb;
    out->flush = flush_supported ? block_flush_cb : NULL; out->ready = 1;
    set_status(status | VIRTIO_STATUS_DRIVER_OK);
    if ((read_reg(REG_STATUS) & VIRTIO_STATUS_FAILED) != 0u) { out->ready = 0; last_error = "device entered FAILED state during startup"; return 0; }
    driver_ready = 1; last_error = "ready";
    return 1;
}
