#include <stddef.h>
#include <stdint.h>
#include "chos/entropy.h"
#include "chos/platform.h"

#define VIRTIO_MAGIC UINT32_C(0x74726976)
#define VIRTIO_DEVICE_RNG 4u
#define VIRTIO_F_VERSION_1 (UINT32_C(1) << 0u)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u
#define VQ_DESC_F_WRITE 2u
#define RNG_SLOT_SIZE 0x200u
#define RNG_SLOT_COUNT 32u
#define RNG_VQ_SIZE 8u
#define RNG_BUFFER_SIZE 512u

/* VirtIO-MMIO transport offsets (version 2 / modern transport). */
enum {
    REG_MAGIC = 0x000, REG_VERSION = 0x004, REG_DEVICE_ID = 0x008,
    REG_DEVICE_FEATURES = 0x010, REG_DEVICE_FEATURES_SEL = 0x014,
    REG_DRIVER_FEATURES = 0x020, REG_DRIVER_FEATURES_SEL = 0x024,
    REG_QUEUE_SEL = 0x030, REG_QUEUE_NUM_MAX = 0x034, REG_QUEUE_NUM = 0x038,
    REG_QUEUE_READY = 0x044, REG_QUEUE_NOTIFY = 0x050, REG_STATUS = 0x070,
    REG_QUEUE_DESC_LOW = 0x080, REG_QUEUE_DESC_HIGH = 0x084,
    REG_QUEUE_DRIVER_LOW = 0x090, REG_QUEUE_DRIVER_HIGH = 0x094,
    REG_QUEUE_DEVICE_LOW = 0x0a0, REG_QUEUE_DEVICE_HIGH = 0x0a4
};

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} rng_vq_desc_t;

typedef struct __attribute__((aligned(64))) {
    volatile uint16_t flags;
    volatile uint16_t index;
    volatile uint16_t ring[RNG_VQ_SIZE];
    volatile uint16_t used_event;
} rng_vq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t length;
} rng_vq_used_element_t;

typedef struct __attribute__((aligned(64))) {
    volatile uint16_t flags;
    volatile uint16_t index;
    volatile rng_vq_used_element_t ring[RNG_VQ_SIZE];
    volatile uint16_t avail_event;
} rng_vq_used_t;

_Static_assert(sizeof(rng_vq_desc_t) == 16u, "VirtIO descriptor ABI");
_Static_assert(sizeof(rng_vq_used_element_t) == 8u, "VirtIO used element ABI");

static rng_vq_desc_t rng_descriptors[RNG_VQ_SIZE] __attribute__((aligned(64)));
static rng_vq_avail_t rng_available __attribute__((aligned(64)));
static rng_vq_used_t rng_used __attribute__((aligned(64)));
static uint8_t rng_buffer[RNG_BUFFER_SIZE] __attribute__((aligned(64)));
static uintptr_t rng_mmio;
static uint16_t rng_queue_size;
static uint16_t rng_available_index;
static uint16_t rng_last_used;
static size_t rng_pending_length;
static size_t rng_pending_offset;
static int rng_is_ready;
static const char *rng_error = "VirtIO RNG probe not run";

#ifdef CHOS_HOST_TEST
#define MOCK_MMIO_BASE ((uintptr_t)0x10000000u)
#define MOCK_REGISTER_COUNT (RNG_SLOT_SIZE / sizeof(uint32_t))
static volatile uint32_t mock_registers[RNG_SLOT_COUNT][MOCK_REGISTER_COUNT];
static uint32_t mock_features[RNG_SLOT_COUNT][2];
static uintptr_t mock_base = MOCK_MMIO_BASE;
static unsigned mock_slot(uintptr_t base) {
    if (base < mock_base || (base - mock_base) / RNG_SLOT_SIZE >= RNG_SLOT_COUNT) return RNG_SLOT_COUNT;
    return (unsigned)((base - mock_base) / RNG_SLOT_SIZE);
}
static uintptr_t mmio_base(void) { return mock_base; }
static uint32_t mmio_read(uintptr_t base, uint32_t offset) {
    unsigned slot = mock_slot(base);
    if (slot >= RNG_SLOT_COUNT || offset >= RNG_SLOT_SIZE || (offset & 3u) != 0u) return 0u;
    if (offset == REG_DEVICE_FEATURES) {
        uint32_t selector = mock_registers[slot][REG_DEVICE_FEATURES_SEL / 4u];
        return mock_features[slot][selector & 1u];
    }
    return mock_registers[slot][offset / 4u];
}
static void mmio_write(uintptr_t base, uint32_t offset, uint32_t value) {
    unsigned slot = mock_slot(base);
    if (slot >= RNG_SLOT_COUNT || offset >= RNG_SLOT_SIZE || (offset & 3u) != 0u) return;
    if (offset == REG_STATUS && value == 0u) {
        mock_registers[slot][offset / 4u] = 0u;
        return;
    }
    mock_registers[slot][offset / 4u] = value;
}
static void clean_memory(const void *ptr, size_t length) { (void)ptr; (void)length; }
static void invalidate_memory(const void *ptr, size_t length) { (void)ptr; (void)length; }
static void io_barrier(void) { __asm__ volatile("" ::: "memory"); }
#else
static uintptr_t mmio_base(void) { return platform_virtio_mmio_base(); }
static uint32_t mmio_read(uintptr_t base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}
static void mmio_write(uintptr_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static size_t cache_line_size(void) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    {
        size_t size = (size_t)4u << ((ctr >> 16u) & 0x0fu);
        return size >= 4u && size <= 2048u && (size & (size - 1u)) == 0u ? size : 64u;
    }
}
static void clean_memory(const void *ptr, size_t length) {
    uintptr_t start, end, address;
    size_t line;
    if (ptr == NULL || length == 0u) return;
    line = cache_line_size(); start = (uintptr_t)ptr & ~((uintptr_t)line - 1u);
    end = (uintptr_t)ptr + length;
    if (end < (uintptr_t)ptr || end > UINTPTR_MAX - (line - 1u)) return;
    end = (end + line - 1u) & ~((uintptr_t)line - 1u);
    for (address = start; address < end; address += line)
        __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
    __asm__ volatile("dsb osh" ::: "memory");
}
static void invalidate_memory(const void *ptr, size_t length) {
    uintptr_t start, end, address;
    size_t line;
    if (ptr == NULL || length == 0u) return;
    line = cache_line_size(); start = (uintptr_t)ptr & ~((uintptr_t)line - 1u);
    end = (uintptr_t)ptr + length;
    if (end < (uintptr_t)ptr || end > UINTPTR_MAX - (line - 1u)) return;
    end = (end + line - 1u) & ~((uintptr_t)line - 1u);
    for (address = start; address < end; address += line)
        __asm__ volatile("dc ivac, %0" :: "r"(address) : "memory");
    __asm__ volatile("dsb osh" ::: "memory");
}
static void io_barrier(void) { __asm__ volatile("dmb osh" ::: "memory"); }
#endif

static void zero_bytes(void *ptr, size_t length) {
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0u; i < length; ++i) p[i] = 0u;
}

static void set_failed(uintptr_t base, const char *message) {
    if (base != 0u) mmio_write(base, REG_STATUS, VIRTIO_STATUS_FAILED);
    rng_is_ready = 0;
    rng_error = message;
}

static int post_buffer(void) {
    uint16_t slot;
    if (!rng_is_ready || rng_queue_size == 0u) return 0;
    rng_descriptors[0].address = (uint64_t)(uintptr_t)rng_buffer;
    rng_descriptors[0].length = RNG_BUFFER_SIZE;
    rng_descriptors[0].flags = VQ_DESC_F_WRITE;
    rng_descriptors[0].next = 0u;
    slot = (uint16_t)(rng_available_index % rng_queue_size);
    rng_available.ring[slot] = 0u;
    clean_memory(rng_descriptors, sizeof(rng_descriptors));
    clean_memory(rng_buffer, sizeof(rng_buffer));
    clean_memory((const void *)&rng_available, sizeof(rng_available));
    io_barrier();
    rng_available_index++;
    rng_available.index = rng_available_index;
    clean_memory((const void *)&rng_available, sizeof(rng_available));
    mmio_write(rng_mmio, REG_QUEUE_NOTIFY, 0u);
    return 1;
}

static int initialize_device(uintptr_t base) {
    uint32_t status, version, feature_high, queue_max;
    uint16_t size = RNG_VQ_SIZE;
    unsigned reset_polls;
    mmio_write(base, REG_STATUS, 0u);
    for (reset_polls = 0u; reset_polls < 1000u; ++reset_polls) {
        status = mmio_read(base, REG_STATUS);
        if (status == 0u) break;
    }
    if (reset_polls == 1000u) { set_failed(base, "VirtIO RNG reset timed out"); return 0; }
    mmio_write(base, REG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write(base, REG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    version = mmio_read(base, REG_VERSION);
    if (version != 2u) { set_failed(base, "VirtIO RNG requires modern MMIO version 2"); return 0; }
    mmio_write(base, REG_DEVICE_FEATURES_SEL, 1u);
    feature_high = mmio_read(base, REG_DEVICE_FEATURES);
    if ((feature_high & VIRTIO_F_VERSION_1) == 0u) {
        set_failed(base, "VirtIO RNG did not negotiate VERSION_1"); return 0;
    }
    mmio_write(base, REG_DRIVER_FEATURES_SEL, 0u);
    mmio_write(base, REG_DRIVER_FEATURES, 0u);
    mmio_write(base, REG_DRIVER_FEATURES_SEL, 1u);
    mmio_write(base, REG_DRIVER_FEATURES, VIRTIO_F_VERSION_1);
    mmio_write(base, REG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    status = mmio_read(base, REG_STATUS);
    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        set_failed(base, "VirtIO RNG rejected required features"); return 0;
    }
    mmio_write(base, REG_QUEUE_SEL, 0u);
    queue_max = mmio_read(base, REG_QUEUE_NUM_MAX);
    if (queue_max == 0u) { set_failed(base, "VirtIO RNG has no receive queue"); return 0; }
    while ((uint32_t)size > queue_max) size = (uint16_t)(size >> 1u);
    if (size == 0u) size = 1u;

    zero_bytes(rng_descriptors, sizeof(rng_descriptors));
    zero_bytes((void *)&rng_available, sizeof(rng_available));
    zero_bytes((void *)&rng_used, sizeof(rng_used));
    zero_bytes(rng_buffer, sizeof(rng_buffer));
    rng_mmio = base;
    rng_queue_size = size;
    rng_available_index = 0u;
    rng_last_used = 0u;
    rng_pending_length = 0u;
    rng_pending_offset = 0u;
    rng_descriptors[0].address = (uint64_t)(uintptr_t)rng_buffer;
    rng_descriptors[0].length = RNG_BUFFER_SIZE;
    rng_descriptors[0].flags = VQ_DESC_F_WRITE;
    rng_descriptors[0].next = 0u;
    rng_available.ring[0] = 0u;
    rng_available.index = 1u;
    rng_available_index = 1u;
    clean_memory(rng_descriptors, sizeof(rng_descriptors));
    clean_memory(rng_buffer, sizeof(rng_buffer));
    clean_memory((const void *)&rng_available, sizeof(rng_available));
    clean_memory((const void *)&rng_used, sizeof(rng_used));
    mmio_write(base, REG_QUEUE_NUM, size);
    mmio_write(base, REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)rng_descriptors);
    mmio_write(base, REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)rng_descriptors >> 32u));
    mmio_write(base, REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)&rng_available);
    mmio_write(base, REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)&rng_available >> 32u));
    mmio_write(base, REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)&rng_used);
    mmio_write(base, REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)&rng_used >> 32u));
    mmio_write(base, REG_QUEUE_READY, 1u);
    rng_is_ready = 1;
    mmio_write(base, REG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                                VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    io_barrier();
    mmio_write(base, REG_QUEUE_NOTIFY, 0u);
    rng_error = "VirtIO RNG initialized; waiting for device bytes";
    return 1;
}

int chos_entropy_init(void) {
    uintptr_t base = mmio_base();
    unsigned slot;
    if (rng_is_ready) return 1; /* preserve live DMA and unconsumed entropy */
    rng_is_ready = 0;
    rng_mmio = 0u;
    rng_queue_size = 0u;
    rng_available_index = 0u;
    rng_last_used = 0u;
    rng_pending_length = 0u;
    rng_pending_offset = 0u;
    rng_error = "no VirtIO RNG device found";
    zero_bytes(rng_descriptors, sizeof(rng_descriptors));
    zero_bytes((void *)&rng_available, sizeof(rng_available));
    zero_bytes((void *)&rng_used, sizeof(rng_used));
    zero_bytes(rng_buffer, sizeof(rng_buffer));
    if (base == 0u) { rng_error = "VirtIO MMIO aperture unavailable"; return 0; }
    for (slot = 0u; slot < RNG_SLOT_COUNT; ++slot) {
        uintptr_t device = base + (uintptr_t)slot * RNG_SLOT_SIZE;
        if (mmio_read(device, REG_MAGIC) != VIRTIO_MAGIC ||
            mmio_read(device, REG_DEVICE_ID) != VIRTIO_DEVICE_RNG) continue;
        if (initialize_device(device)) return 1;
    }
    return 0;
}

int chos_entropy_ready(void) { return rng_is_ready; }
const char *chos_entropy_last_error(void) { return rng_error; }

static int poll_completion(void) {
    uint16_t device_index, ring_slot;
    uint32_t id, length;
    if (!rng_is_ready || rng_pending_length > rng_pending_offset) return rng_pending_length > rng_pending_offset;
    invalidate_memory((const void *)&rng_used, sizeof(rng_used));
    device_index = rng_used.index;
    if (device_index == rng_last_used) return 0;
    if ((uint16_t)(device_index-rng_last_used)!=1u) {
        set_failed(rng_mmio,"VirtIO RNG used-ring overrun"); return 0;
    }
    ring_slot = (uint16_t)(rng_last_used % rng_queue_size);
    io_barrier();
    id = rng_used.ring[ring_slot].id;
    length = rng_used.ring[ring_slot].length;
    rng_last_used++;
    if (id != 0u || length == 0u || length > RNG_BUFFER_SIZE) {
        set_failed(rng_mmio, "VirtIO RNG returned an invalid used-ring entry");
        return 0;
    }
    invalidate_memory(rng_buffer, length);
    rng_pending_length = length;
    rng_pending_offset = 0u;
    rng_error = "VirtIO RNG data available";
    return 1;
}

size_t chos_entropy_read(void *destination, size_t capacity) {
    uint8_t *out = (uint8_t *)destination;
    size_t available, amount, i;
    if (destination == NULL || capacity == 0u || capacity > CHOS_ENTROPY_MAX_REQUEST || !rng_is_ready)
        return 0u;
    if (!poll_completion()) return 0u;
    available = rng_pending_length - rng_pending_offset;
    amount = capacity < available ? capacity : available;
    for (i = 0u; i < amount; ++i) out[i] = rng_buffer[rng_pending_offset + i];
    rng_pending_offset += amount;
    if (rng_pending_offset == rng_pending_length) {
        zero_bytes(rng_buffer, sizeof(rng_buffer));
        rng_pending_length = 0u;
        rng_pending_offset = 0u;
        if (!post_buffer()) { set_failed(rng_mmio, "VirtIO RNG could not repost its receive buffer"); return 0u; }
    }
    return amount;
}

#ifdef CHOS_HOST_TEST
void chos_entropy_test_reset(void) {
    zero_bytes((void *)mock_registers, sizeof(mock_registers));
    zero_bytes((void *)mock_features, sizeof(mock_features));
    rng_is_ready = 0; rng_mmio = 0u; rng_queue_size = 0u;
    rng_available_index = 0u; rng_last_used = 0u; rng_pending_length = 0u; rng_pending_offset = 0u;
    rng_error = "test reset";
}
void chos_entropy_test_add_device(unsigned slot, uint32_t version, uint32_t device_id,
                                  uint32_t feature_version_1, uint32_t queue_max) {
    if (slot >= RNG_SLOT_COUNT) return;
    mock_registers[slot][REG_MAGIC / 4u] = VIRTIO_MAGIC;
    mock_registers[slot][REG_VERSION / 4u] = version;
    mock_registers[slot][REG_DEVICE_ID / 4u] = device_id;
    mock_registers[slot][REG_QUEUE_NUM_MAX / 4u] = queue_max;
    mock_features[slot][1] = feature_version_1 ? VIRTIO_F_VERSION_1 : 0u;
}
int chos_entropy_test_supply(const uint8_t *data, size_t length) {
    uint16_t slot;
    size_t i;
    if (!rng_is_ready || data == NULL || length == 0u || length > RNG_BUFFER_SIZE ||
        rng_pending_length != rng_pending_offset) return 0;
    slot = (uint16_t)(rng_used.index % rng_queue_size);
    for (i = 0u; i < length; ++i) rng_buffer[i] = data[i];
    rng_used.ring[slot].id = 0u;
    rng_used.ring[slot].length = (uint32_t)length;
    rng_used.index++;
    return 1;
}
#endif
