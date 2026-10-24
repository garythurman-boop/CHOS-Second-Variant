#include <stddef.h>
#include <stdint.h>
#include "chos/platform.h"
#include "chos/virtio_transport.h"
#include "chos/virtio_gpu.h"

#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_DEVICE_GPU 16u
#define VIRTIO_F_VERSION_1 (1u << 0)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u
#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u
#define VIRTIO_MMIO_SLOT_SIZE 0x200u
#define VIRTIO_MMIO_SLOT_COUNT 32u
#define GPU_VQ_SIZE 8u
#define GPU_WAIT_LIMIT 20000000u
#define GPU_RESOURCE_ID_BASE 1u
#define GPU_MAX_FRAMEBUFFER_BYTES (CHOS_DISPLAY_MAX_WIDTH * CHOS_DISPLAY_MAX_HEIGHT * 4u)
#define GPU_FORMAT_B8G8R8A8_UNORM 1u
#define GPU_CMD_GET_DISPLAY_INFO 0x0100u
#define GPU_CMD_RESOURCE_CREATE_2D 0x0101u
#define GPU_CMD_RESOURCE_UNREF 0x0102u
#define GPU_CMD_SET_SCANOUT 0x0103u
#define GPU_CMD_RESOURCE_FLUSH 0x0104u
#define GPU_CMD_TRANSFER_TO_HOST_2D 0x0105u
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define GPU_CMD_UPDATE_CURSOR 0x0300u
#define GPU_CMD_MOVE_CURSOR 0x0301u
#define GPU_FLAG_FENCE 0x00000001u
#define GPU_CURSOR_RESOURCE_ID 0x10000u
#define GPU_CURSOR_SIZE 64u
#define GPU_RESP_OK_NODATA 0x1100u
#define GPU_RESP_OK_DISPLAY_INFO 0x1101u

/* VirtIO-MMIO v2 common register offsets. */
enum {
    REG_MAGIC=0x000, REG_VERSION=0x004, REG_DEVICE_ID=0x008,
    REG_DEVICE_FEATURES=0x010, REG_DEVICE_FEATURES_SEL=0x014,
    REG_DRIVER_FEATURES=0x020, REG_DRIVER_FEATURES_SEL=0x024,
    REG_QUEUE_SEL=0x030, REG_QUEUE_NUM_MAX=0x034, REG_QUEUE_NUM=0x038,
    REG_QUEUE_READY=0x044, REG_QUEUE_NOTIFY=0x050,
    REG_STATUS=0x070, REG_QUEUE_DESC_LOW=0x080, REG_QUEUE_DESC_HIGH=0x084,
    REG_QUEUE_DRIVER_LOW=0x090, REG_QUEUE_DRIVER_HIGH=0x094,
    REG_QUEUE_DEVICE_LOW=0x0a0, REG_QUEUE_DEVICE_HIGH=0x0a4
};

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t ring_idx;
    uint8_t padding[3];
} gpu_ctrl_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t x, y, width, height;
} gpu_rect_t;

typedef struct __attribute__((packed)) {
    gpu_rect_t rect;
    uint32_t enabled;
    uint32_t flags;
} gpu_display_one_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    gpu_display_one_t scanouts[16];
} gpu_display_info_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    uint32_t resource_id, format, width, height;
} gpu_resource_create_2d_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
} gpu_resource_unreference_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    gpu_rect_t rect;
    uint32_t scanout_id, resource_id;
} gpu_set_scanout_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    uint32_t resource_id, nr_entries;
    uint64_t addr;
    uint32_t length, padding;
} gpu_attach_backing_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    gpu_rect_t rect;
    uint64_t offset;
    uint32_t resource_id, padding;
} gpu_transfer_to_host_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    gpu_rect_t rect;
    uint32_t resource_id, padding;
} gpu_resource_flush_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
} gpu_mem_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t scanout_id, x, y, padding;
} gpu_cursor_position_t;

typedef struct __attribute__((packed)) {
    gpu_ctrl_hdr_t hdr;
    gpu_cursor_position_t pos;
    uint32_t resource_id, hot_x, hot_y, cursor_padding;
} gpu_cursor_command_t;

_Static_assert(sizeof(gpu_cursor_command_t) == 56u, "VirtIO-GPU cursor command ABI");

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) gpu_virtq_desc_t;


_Static_assert(sizeof(gpu_ctrl_hdr_t) == 24u, "VirtIO-GPU control header ABI");
_Static_assert(sizeof(gpu_display_one_t) == 24u, "VirtIO-GPU display mode ABI");
_Static_assert(sizeof(gpu_display_info_t) == 408u, "VirtIO-GPU display-info ABI");
_Static_assert(sizeof(gpu_attach_backing_t) == 48u, "VirtIO-GPU attach-backing ABI");
_Static_assert(sizeof(gpu_virtq_desc_t) == 16u, "Virtqueue descriptor ABI");

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) gpu_virtq_used_elem_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[GPU_VQ_SIZE];
    volatile uint16_t used_event;
} __attribute__((aligned(2))) gpu_virtq_avail_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile gpu_virtq_used_elem_t ring[GPU_VQ_SIZE];
    volatile uint16_t avail_event;
} __attribute__((aligned(4))) gpu_virtq_used_t;

static gpu_virtq_desc_t queue_desc[GPU_VQ_SIZE] __attribute__((aligned(16)));
static gpu_virtq_avail_t queue_avail __attribute__((aligned(2)));
static gpu_virtq_used_t queue_used __attribute__((aligned(4)));
static uint8_t request_buffer[128] __attribute__((aligned(16)));
static uint8_t response_buffer[512] __attribute__((aligned(16)));
/* VirtIO-GPU has a dedicated cursorq so pointer motion is not serialized behind
 * scanout transfers on controlq. Keep its rings independent of controlq. */
static gpu_virtq_desc_t cursor_queue_desc[GPU_VQ_SIZE] __attribute__((aligned(16)));
static gpu_virtq_avail_t cursor_queue_avail __attribute__((aligned(2)));
static gpu_virtq_used_t cursor_queue_used __attribute__((aligned(4)));
static uint8_t cursor_request_buffer[sizeof(gpu_cursor_command_t)] __attribute__((aligned(16)));
static uint8_t cursor_response_buffer[sizeof(gpu_ctrl_hdr_t)] __attribute__((aligned(16)));
static uint32_t cursor_framebuffer[GPU_CURSOR_SIZE * GPU_CURSOR_SIZE] __attribute__((aligned(4096)));
static uint16_t cursor_queue_size;
static uint16_t cursor_available_idx;
static uint32_t cursor_fence_id;
static int cursor_queue_ready;
static int cursor_resource_created;
static int cursor_shape_loaded;
static int cursor_is_visible;
static uint32_t cursor_hot_x, cursor_hot_y;
static uint32_t framebuffer[CHOS_DISPLAY_MAX_WIDTH * CHOS_DISPLAY_MAX_HEIGHT] __attribute__((aligned(4096)));
static chos_surface_t active_surface;
static int resource_created[CHOS_DISPLAY_MODE_COUNT];
static size_t active_mode_index;
static uintptr_t mmio_base;
static uintptr_t detected_base;
static uint16_t queue_size;
static uint16_t available_idx;
static int driver_ready;
/* GOP fallback renders into the normal ARGB backbuffer and copies damage to
 * the firmware framebuffer. This keeps GUI code and color semantics shared. */
static int gop_backend;
static volatile uint8_t *gop_framebuffer;
static uint64_t gop_framebuffer_size;
static uint32_t gop_stride, gop_format;
static uint32_t gop_red_mask, gop_green_mask, gop_blue_mask, gop_reserved_mask;
static const char *last_error = "probe not run";

static uint32_t read_reg(uint32_t offset) { return chos_virtio_read(mmio_base,offset); }
static void write_reg(uint32_t offset, uint32_t value) { chos_virtio_write(mmio_base,offset,value); }
static void dma_store_barrier(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void dma_load_barrier(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static void cpu_full_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void set_status(uint32_t value) { write_reg(REG_STATUS, value); cpu_full_barrier(); }
static void bytes_zero(void *destination, size_t count) {
    uint8_t *p = (uint8_t *)destination;
    size_t i;
    for (i = 0u; i < count; ++i) p[i] = 0u;
}
static void bytes_copy(void *destination, const void *source, size_t count) {
    uint8_t *d = (uint8_t *)destination;
    const uint8_t *s = (const uint8_t *)source;
    size_t i;
    for (i = 0u; i < count; ++i) d[i] = s[i];
}
static void clean_range_to_poc(const void *memory, size_t length) {
    uint64_t ctr;
    uintptr_t line_size, start, end, address;
    if (memory == NULL || length == 0u) return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line_size < 4u || (line_size & (line_size - 1u)) != 0u) line_size = 64u;
    start = (uintptr_t)memory & ~(line_size - 1u);
    end = (uintptr_t)memory + length;
    for (address = start; address < end; address += line_size) {
        __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}
static void invalidate_range_from_poc(const void *memory, size_t length) {
    uint64_t ctr;
    uintptr_t line_size, start, end, address;
    if (memory == NULL || length == 0u) return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line_size < 4u || (line_size & (line_size - 1u)) != 0u) line_size = 64u;
    start = (uintptr_t)memory & ~(line_size - 1u);
    end = (uintptr_t)memory + length;
    for (address = start; address < end; address += line_size) {
        __asm__ volatile("dc ivac, %0" :: "r"(address) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void initialize_ring(void) {
    uint16_t i;
    bytes_zero(queue_desc, sizeof(queue_desc));
    bytes_zero((void *)&queue_avail, sizeof(queue_avail));
    bytes_zero((void *)&queue_used, sizeof(queue_used));
    for (i = 0u; i < GPU_VQ_SIZE; ++i) {
        queue_avail.ring[i] = 0u;
        queue_used.ring[i].id = 0u;
        queue_used.ring[i].len = 0u;
    }
    available_idx = 0u;
}

static void initialize_cursor_ring(void) {
    uint16_t i;
    bytes_zero(cursor_queue_desc, sizeof(cursor_queue_desc));
    bytes_zero((void *)&cursor_queue_avail, sizeof(cursor_queue_avail));
    bytes_zero((void *)&cursor_queue_used, sizeof(cursor_queue_used));
    for (i = 0u; i < GPU_VQ_SIZE; ++i) {
        cursor_queue_avail.ring[i] = 0u;
        cursor_queue_used.ring[i].id = 0u;
        cursor_queue_used.ring[i].len = 0u;
    }
    cursor_available_idx = 0u;
}

static int setup_cursor_queue(void) {
    uint32_t max_queue;
    write_reg(REG_QUEUE_SEL, 1u);
    max_queue = read_reg(REG_QUEUE_NUM_MAX);
    if (max_queue < 2u) {
        cursor_queue_ready = 0;
        write_reg(REG_QUEUE_SEL, 0u);
        return 0; /* Software cursor fallback remains available. */
    }
    cursor_queue_size = GPU_VQ_SIZE;
    while (cursor_queue_size > max_queue) cursor_queue_size >>= 1;
    if (cursor_queue_size < 2u) {
        cursor_queue_ready = 0;
        write_reg(REG_QUEUE_SEL, 0u);
        return 0;
    }
    initialize_cursor_ring();
    clean_range_to_poc(cursor_queue_desc, sizeof(cursor_queue_desc));
    clean_range_to_poc((const void *)&cursor_queue_avail, sizeof(cursor_queue_avail));
    clean_range_to_poc((const void *)&cursor_queue_used, sizeof(cursor_queue_used));
    write_reg(REG_QUEUE_NUM, cursor_queue_size);
    write_reg(REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)cursor_queue_desc);
    write_reg(REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)cursor_queue_desc >> 32));
    write_reg(REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)&cursor_queue_avail);
    write_reg(REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)&cursor_queue_avail >> 32));
    write_reg(REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)&cursor_queue_used);
    write_reg(REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)&cursor_queue_used >> 32));
    write_reg(REG_QUEUE_READY, 1u);
    cpu_full_barrier();
    cursor_queue_ready = 1;
    write_reg(REG_QUEUE_SEL, 0u);
    return 1;
}

static int send_cursor_command(const gpu_cursor_command_t *command) {
    uint16_t used_before;
    uint32_t spin;
    volatile gpu_ctrl_hdr_t *response;
    if (!driver_ready || !cursor_queue_ready || command == NULL) return 0;
    bytes_zero(cursor_request_buffer, sizeof(cursor_request_buffer));
    bytes_zero(cursor_response_buffer, sizeof(cursor_response_buffer));
    bytes_copy(cursor_request_buffer, command, sizeof(*command));
    cursor_queue_desc[0].addr = (uint64_t)(uintptr_t)cursor_request_buffer;
    cursor_queue_desc[0].len = (uint32_t)sizeof(*command);
    cursor_queue_desc[0].flags = VIRTQ_DESC_F_NEXT;
    cursor_queue_desc[0].next = 1u;
    cursor_queue_desc[1].addr = (uint64_t)(uintptr_t)cursor_response_buffer;
    cursor_queue_desc[1].len = (uint32_t)sizeof(cursor_response_buffer);
    cursor_queue_desc[1].flags = VIRTQ_DESC_F_WRITE;
    cursor_queue_desc[1].next = 0u;
    clean_range_to_poc(cursor_queue_desc, sizeof(cursor_queue_desc));
    clean_range_to_poc(cursor_request_buffer, sizeof(*command));
    clean_range_to_poc(cursor_response_buffer, sizeof(cursor_response_buffer));
    used_before = cursor_queue_used.idx;
    cursor_queue_avail.ring[cursor_available_idx & (cursor_queue_size - 1u)] = 0u;
    dma_store_barrier();
    ++cursor_available_idx;
    cursor_queue_avail.idx = cursor_available_idx;
    clean_range_to_poc((const void *)&cursor_queue_avail, sizeof(cursor_queue_avail));
    write_reg(REG_QUEUE_NOTIFY, 1u);
    for (spin = 0u; spin < GPU_WAIT_LIMIT; ++spin) {
        if ((spin & 0xffu) == 0u) invalidate_range_from_poc((const void *)&cursor_queue_used, sizeof(cursor_queue_used));
        dma_load_barrier();
        if (cursor_queue_used.idx != used_before) break;
    }
    if (spin == GPU_WAIT_LIMIT) {
        cursor_queue_ready = 0;
        cursor_is_visible = 0;
        last_error = "VirtIO-GPU cursor queue timed out; using software cursor fallback";
        return 0;
    }
    invalidate_range_from_poc((const void *)&cursor_queue_used, sizeof(cursor_queue_used));
    if (cursor_queue_used.ring[used_before & (cursor_queue_size - 1u)].id != 0u) {
        cursor_queue_ready = 0;
        cursor_is_visible = 0;
        last_error = "VirtIO-GPU cursor queue returned an invalid descriptor";
        return 0;
    }
    invalidate_range_from_poc(cursor_response_buffer, sizeof(cursor_response_buffer));
    response = (volatile gpu_ctrl_hdr_t *)(void *)cursor_response_buffer;
    if (response->type != GPU_RESP_OK_NODATA) {
        /* Cursor failures must not quarantine the otherwise working display. */
        last_error = "VirtIO-GPU rejected a cursor command; using software cursor fallback";
        cursor_queue_ready = 0;
        cursor_is_visible = 0;
        return 0;
    }
    last_error = "ok";
    return 1;
}

static int send_command(const void *request, size_t request_length,
                        size_t response_capacity, uint32_t expected_response) {
    uint16_t used_before;
    uint32_t spin;
    volatile gpu_ctrl_hdr_t *response;
    if (!driver_ready || request == NULL || request_length < sizeof(gpu_ctrl_hdr_t) ||
        request_length > sizeof(request_buffer) || response_capacity < sizeof(gpu_ctrl_hdr_t) ||
        response_capacity > sizeof(response_buffer)) return 0;
    bytes_zero(request_buffer, sizeof(request_buffer));
    bytes_zero(response_buffer, sizeof(response_buffer));
    bytes_copy(request_buffer, request, request_length);
    queue_desc[0].addr = (uint64_t)(uintptr_t)request_buffer;
    queue_desc[0].len = (uint32_t)request_length;
    queue_desc[0].flags = VIRTQ_DESC_F_NEXT;
    queue_desc[0].next = 1u;
    queue_desc[1].addr = (uint64_t)(uintptr_t)response_buffer;
    queue_desc[1].len = (uint32_t)response_capacity;
    queue_desc[1].flags = VIRTQ_DESC_F_WRITE;
    queue_desc[1].next = 0u;
    clean_range_to_poc(queue_desc, sizeof(queue_desc));
    clean_range_to_poc(request_buffer, request_length);
    clean_range_to_poc(response_buffer, response_capacity);
    used_before = queue_used.idx;
    queue_avail.ring[available_idx & (queue_size - 1u)] = 0u;
    dma_store_barrier();
    ++available_idx;
    queue_avail.idx = available_idx;
    clean_range_to_poc((const void *)&queue_avail, sizeof(queue_avail));
    write_reg(REG_QUEUE_NOTIFY, 0u);
    for (spin = 0u; spin < GPU_WAIT_LIMIT; ++spin) {
        if ((spin & 0xffu) == 0u) invalidate_range_from_poc((const void *)&queue_used, sizeof(queue_used));
        dma_load_barrier();
        if (queue_used.idx != used_before) break;
    }
    if (spin == GPU_WAIT_LIMIT) {
        driver_ready = 0;
        last_error = "VirtIO-GPU command timed out; display driver quarantined";
        return 0;
    }
    invalidate_range_from_poc((const void *)&queue_used, sizeof(queue_used));
    if (queue_used.ring[used_before & (queue_size - 1u)].id != 0u) {
        driver_ready = 0;
        last_error = "VirtIO-GPU returned an invalid queue descriptor";
        return 0;
    }
    dma_load_barrier();
    invalidate_range_from_poc(response_buffer, response_capacity);
    response = (volatile gpu_ctrl_hdr_t *)(void *)response_buffer;
    if (response->type != expected_response) {
        if (response->type >= 0x1200u) {
            /* A completed guest command can be rejected without invalidating the queue.
             * This is important for display-mode rollback: keep the active mode usable. */
            last_error = "VirtIO-GPU rejected a display command";
        } else {
            last_error = "unexpected VirtIO-GPU response type";
            driver_ready = 0;
        }
        return 0;
    }
    if ((((const gpu_ctrl_hdr_t *)request)->flags & GPU_FLAG_FENCE) != 0u &&
        (((response->flags & GPU_FLAG_FENCE) == 0u) ||
         response->fence_id != ((const gpu_ctrl_hdr_t *)request)->fence_id)) {
        last_error = "VirtIO-GPU fence completion did not match the request";
        driver_ready = 0;
        return 0;
    }
    last_error = "ok";
    return 1;
}

static int send_simple(const gpu_ctrl_hdr_t *request, size_t length) {
    return send_command(request, length, sizeof(gpu_ctrl_hdr_t), GPU_RESP_OK_NODATA);
}

static int query_display_info(void) {
    gpu_display_info_t info;
    bytes_zero(&info, sizeof(info));
    info.hdr.type = GPU_CMD_GET_DISPLAY_INFO;
    return send_command(&info.hdr, sizeof(info.hdr), sizeof(info), GPU_RESP_OK_DISPLAY_INFO);
}

static void unreference_display_resource(size_t mode_index) {
    gpu_resource_unreference_t unref;
    const char *original_error = last_error;
    if (mode_index >= CHOS_DISPLAY_MODE_COUNT || !driver_ready) return;
    bytes_zero(&unref, sizeof(unref));
    unref.hdr.type = GPU_CMD_RESOURCE_UNREF;
    unref.resource_id = GPU_RESOURCE_ID_BASE + (uint32_t)mode_index;
    (void)send_simple(&unref.hdr, sizeof(unref));
    resource_created[mode_index] = 0;
    if (driver_ready && original_error != NULL) last_error = original_error;
}

static int create_display_resource(size_t mode_index) {
    chos_display_mode_t mode;
    gpu_resource_create_2d_t create;
    gpu_attach_backing_t attach;
    uint32_t resource_id;
    size_t backing_bytes;
    if (!chos_display_mode_get(mode_index, &mode)) return 0;
    resource_id = GPU_RESOURCE_ID_BASE + (uint32_t)mode_index;
    backing_bytes = (size_t)mode.width * (size_t)mode.height * sizeof(uint32_t);
    if (backing_bytes > sizeof(framebuffer) || backing_bytes > UINT32_MAX) return 0;

    bytes_zero(&create, sizeof(create));
    create.hdr.type = GPU_CMD_RESOURCE_CREATE_2D;
    create.resource_id = resource_id;
    create.format = GPU_FORMAT_B8G8R8A8_UNORM;
    create.width = mode.width;
    create.height = mode.height;
    if (!send_simple(&create.hdr, sizeof(create))) return 0;

    bytes_zero(&attach, sizeof(attach));
    attach.hdr.type = GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach.resource_id = resource_id;
    attach.nr_entries = 1u;
    attach.addr = (uint64_t)(uintptr_t)framebuffer;
    attach.length = (uint32_t)backing_bytes;
    if (!send_simple(&attach.hdr, sizeof(attach))) {
        unreference_display_resource(mode_index);
        return 0;
    }
    resource_created[mode_index] = 1;
    return 1;
}

static int activate_scanout(size_t mode_index) {
    chos_display_mode_t mode;
    gpu_set_scanout_t scanout;
    if (!chos_display_mode_get(mode_index, &mode) || !resource_created[mode_index]) return 0;
    bytes_zero(&scanout, sizeof(scanout));
    scanout.hdr.type = GPU_CMD_SET_SCANOUT;
    scanout.rect.x = 0u;
    scanout.rect.y = 0u;
    scanout.rect.width = mode.width;
    scanout.rect.height = mode.height;
    scanout.scanout_id = 0u;
    scanout.resource_id = GPU_RESOURCE_ID_BASE + (uint32_t)mode_index;
    return send_simple(&scanout.hdr, sizeof(scanout));
}

int virtio_gpu_init(chos_surface_t *surface) {
    uintptr_t first_base;
    uint32_t slot, features0, features1, driver_features0 = 0u;
    uint32_t status, max_queue;
    int found = 0;
    if (surface == NULL) { last_error = "invalid framebuffer output pointer"; return 0; }
    size_t i;
    if(mmio_base || driver_ready) {last_error="VirtIO-GPU already initialized or quarantined; reboot before rebinding";return 0;}
    driver_ready = 0;
    gop_backend = 0;
    gop_framebuffer = NULL;
    gop_framebuffer_size = 0u;
    cursor_queue_ready = 0;
    cursor_resource_created = 0;
    cursor_shape_loaded = 0;
    cursor_is_visible = 0;
    cursor_fence_id = 0u;
    for (i = 0u; i < CHOS_DISPLAY_MODE_COUNT; ++i) resource_created[i] = 0;
    active_mode_index = 0u;
    detected_base = 0u;
    first_base = chos_virtio_aperture();
    if (first_base == 0u) { last_error = "platform has no VirtIO-MMIO aperture"; return 0; }
    for (slot = 0u; slot < VIRTIO_MMIO_SLOT_COUNT; ++slot) {
        uintptr_t candidate = first_base + (uintptr_t)slot * VIRTIO_MMIO_SLOT_SIZE;
        mmio_base = candidate;
        if (read_reg(REG_MAGIC) != VIRTIO_MAGIC || read_reg(REG_DEVICE_ID) != VIRTIO_DEVICE_GPU) continue;
        if (read_reg(REG_VERSION) < 2u) { last_error = "VirtIO-GPU found but legacy MMIO is unsupported"; return 0; }
        detected_base = candidate;
        found = 1;
        break;
    }
    if (!found) { mmio_base = 0u; last_error = "no VirtIO-GPU device found in 32 MMIO slots"; return 0; }

    set_status(0u);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    write_reg(REG_DEVICE_FEATURES_SEL, 0u); features0 = read_reg(REG_DEVICE_FEATURES);
    write_reg(REG_DEVICE_FEATURES_SEL, 1u); features1 = read_reg(REG_DEVICE_FEATURES);
    (void)features0;
    if ((features1 & VIRTIO_F_VERSION_1) == 0u) {
        set_status(VIRTIO_STATUS_FAILED); last_error = "VirtIO-GPU does not offer VIRTIO_F_VERSION_1"; return 0;
    }
    write_reg(REG_DRIVER_FEATURES_SEL, 0u); write_reg(REG_DRIVER_FEATURES, driver_features0);
    write_reg(REG_DRIVER_FEATURES_SEL, 1u); write_reg(REG_DRIVER_FEATURES, VIRTIO_F_VERSION_1);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    set_status(status);
    if ((read_reg(REG_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        set_status(VIRTIO_STATUS_FAILED); last_error = "VirtIO-GPU rejected negotiated features"; return 0;
    }
    write_reg(REG_QUEUE_SEL, 0u);
    max_queue = read_reg(REG_QUEUE_NUM_MAX);
    if (max_queue < 2u) { set_status(VIRTIO_STATUS_FAILED); last_error = "VirtIO-GPU control queue is too small"; return 0; }
    queue_size = GPU_VQ_SIZE;
    while (queue_size > max_queue) queue_size >>= 1;
    if (queue_size < 2u) { set_status(VIRTIO_STATUS_FAILED); last_error = "VirtIO-GPU control queue has no usable descriptors"; return 0; }
    initialize_ring();
    clean_range_to_poc((const void *)&queue_used, sizeof(queue_used));
    write_reg(REG_QUEUE_NUM, queue_size);
    write_reg(REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)queue_desc);
    write_reg(REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)queue_desc >> 32));
    write_reg(REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)&queue_avail);
    write_reg(REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)&queue_avail >> 32));
    write_reg(REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)&queue_used);
    write_reg(REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)&queue_used >> 32));
    write_reg(REG_QUEUE_READY, 1u);
    cpu_full_barrier();
    (void)setup_cursor_queue();
    write_reg(REG_QUEUE_SEL, 0u);
    set_status(status | VIRTIO_STATUS_DRIVER_OK);
    driver_ready = 1;
    if (!query_display_info() || !create_display_resource(0u) || !activate_scanout(0u)) {
        if (read_reg(REG_STATUS) != 0u) set_status(read_reg(REG_STATUS) | VIRTIO_STATUS_FAILED);
        driver_ready = 0;
        return 0;
    }
    active_surface.pixels = framebuffer;
    active_surface.width = CHOS_GUI_WIDTH;
    active_surface.height = CHOS_GUI_HEIGHT;
    active_surface.stride_pixels = CHOS_GUI_WIDTH;
    active_mode_index = 0u;
    bytes_zero(framebuffer, (size_t)CHOS_GUI_WIDTH * CHOS_GUI_HEIGHT * sizeof(uint32_t));
    *surface = active_surface;
    last_error = "ok";
    return 1;
}

static uint32_t gop_pack_channel(uint8_t channel, uint32_t mask) {
    unsigned shift = 0u;
    uint32_t field = mask;
    uint64_t maximum;
    while (field != 0u && (field & 1u) == 0u) { field >>= 1u; ++shift; }
    if (field == 0u) return 0u;
    maximum = field;
    return (uint32_t)((((uint64_t)channel * maximum + 127u) / 255u) << shift) & mask;
}

int virtio_gpu_init_gop(const chos_boot_info_t *boot_info, chos_surface_t *surface) {
    int mode_index;
    uint64_t required;
    size_t i;
    if (boot_info == NULL || surface == NULL || !chos_boot_info_validate(boot_info) ||
        (boot_info->flags & CHOS_BOOT_FLAG_FRAMEBUFFER) == 0u) {
        last_error = "UEFI GOP framebuffer is unavailable or boot-info is invalid";
        return 0;
    }
    if (boot_info->framebuffer_width > CHOS_DISPLAY_MAX_WIDTH ||
        boot_info->framebuffer_height > CHOS_DISPLAY_MAX_HEIGHT ||
        boot_info->framebuffer_stride < boot_info->framebuffer_width ||
        boot_info->framebuffer_base > UINT64_MAX - boot_info->framebuffer_size) {
        last_error = "UEFI GOP framebuffer exceeds supported surface bounds";
        return 0;
    }
    mode_index = chos_display_mode_index(boot_info->framebuffer_width, boot_info->framebuffer_height);
    if (mode_index < 0) {
        last_error = "UEFI GOP resolution is not currently a supported CHOS display mode";
        return 0;
    }
    if (boot_info->framebuffer_stride > UINT64_MAX / 4u / boot_info->framebuffer_height) {
        last_error = "UEFI GOP framebuffer byte count overflow";
        return 0;
    }
    required = (uint64_t)boot_info->framebuffer_stride * boot_info->framebuffer_height * 4u;
    if (required > boot_info->framebuffer_size || required > SIZE_MAX) {
        last_error = "UEFI GOP framebuffer memory is too small for its mode";
        return 0;
    }
    if (boot_info->framebuffer_pixel_format != CHOS_GOP_PIXEL_RGBX8 &&
        boot_info->framebuffer_pixel_format != CHOS_GOP_PIXEL_BGRX8 &&
        boot_info->framebuffer_pixel_format != CHOS_GOP_PIXEL_BITMASK) {
        last_error = "UEFI GOP uses an unsupported pixel format";
        return 0;
    }
    if (boot_info->framebuffer_pixel_format == CHOS_GOP_PIXEL_BITMASK &&
        (boot_info->framebuffer_red_mask == 0u || boot_info->framebuffer_green_mask == 0u ||
         boot_info->framebuffer_blue_mask == 0u ||
         (boot_info->framebuffer_red_mask & boot_info->framebuffer_green_mask) != 0u ||
         (boot_info->framebuffer_red_mask & boot_info->framebuffer_blue_mask) != 0u ||
         (boot_info->framebuffer_green_mask & boot_info->framebuffer_blue_mask) != 0u)) {
        last_error = "UEFI GOP bitmask channels are invalid";
        return 0;
    }
    for (i = 0u; i < CHOS_DISPLAY_MODE_COUNT; ++i) resource_created[i] = 0;
    driver_ready = 1;
    gop_backend = 1;
    gop_framebuffer = (volatile uint8_t *)(uintptr_t)boot_info->framebuffer_base;
    gop_framebuffer_size = boot_info->framebuffer_size;
    gop_stride = boot_info->framebuffer_stride;
    gop_format = boot_info->framebuffer_pixel_format;
    gop_red_mask = boot_info->framebuffer_red_mask;
    gop_green_mask = boot_info->framebuffer_green_mask;
    gop_blue_mask = boot_info->framebuffer_blue_mask;
    gop_reserved_mask = boot_info->framebuffer_reserved_mask;
    cursor_queue_ready = 0; cursor_resource_created = 0; cursor_shape_loaded = 0; cursor_is_visible = 0;
    cursor_fence_id = 0u; mmio_base = 0u; detected_base = 0u;
    active_mode_index = (size_t)mode_index;
    resource_created[active_mode_index] = 1;
    active_surface.pixels = framebuffer;
    active_surface.width = boot_info->framebuffer_width;
    active_surface.height = boot_info->framebuffer_height;
    active_surface.stride_pixels = boot_info->framebuffer_width;
    bytes_zero(framebuffer, (size_t)active_surface.width * active_surface.height * sizeof(uint32_t));
    *surface = active_surface;
    last_error = "GOP framebuffer backend active; drawing uses CHOS ARGB backbuffer";
    return 1;
}

int virtio_gpu_set_mode(size_t mode_index, chos_surface_t *surface) {
    chos_display_mode_t mode;
    size_t bytes;
    if (surface == NULL || !driver_ready || mode_index >= CHOS_DISPLAY_MODE_COUNT ||
        !chos_display_mode_get(mode_index, &mode)) {
        last_error = "unsupported display mode request";
        return 0;
    }
    if (mode_index == active_mode_index) {
        *surface = active_surface;
        return 1;
    }
    if (gop_backend) {
        last_error = "GOP display mode switching is not implemented; retaining firmware mode";
        return 0;
    }
    if (!resource_created[mode_index] && !create_display_resource(mode_index)) return 0;
    if (!activate_scanout(mode_index)) return 0;
    active_mode_index = mode_index;
    active_surface.pixels = framebuffer;
    active_surface.width = mode.width;
    active_surface.height = mode.height;
    active_surface.stride_pixels = mode.width;
    bytes = (size_t)mode.width * (size_t)mode.height * sizeof(uint32_t);
    bytes_zero(framebuffer, bytes);
    *surface = active_surface;
    last_error = "ok";
    return 1;
}

size_t virtio_gpu_current_mode_index(void) { return active_mode_index; }

static int present_rect_valid(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return driver_ready && active_mode_index < CHOS_DISPLAY_MODE_COUNT &&
           resource_created[active_mode_index] && width != 0u && height != 0u &&
           x < active_surface.width && y < active_surface.height &&
           width <= active_surface.width - x && height <= active_surface.height - y;
}

static int transfer_rect_to_host(const gpu_rect_t *rect, uint32_t resource_id) {
    gpu_transfer_to_host_t transfer;
    uint32_t row;
    uint64_t offset;
    size_t row_bytes;
    if (rect == NULL) return 0;
    row_bytes = (size_t)rect->width * sizeof(uint32_t);
    offset = ((uint64_t)rect->y * active_surface.width + rect->x) * sizeof(uint32_t);
    /* Clean only the cache lines touched by the dirty rectangle, not the full
       screen-sized framebuffer. This matters most at 1440p/1600p/1920p modes. */
    for (row = 0u; row < rect->height; ++row) {
        const uint8_t *line = (const uint8_t *)(framebuffer + ((size_t)rect->y + row) * active_surface.stride_pixels + rect->x);
        clean_range_to_poc(line, row_bytes);
    }
    bytes_zero(&transfer, sizeof(transfer));
    transfer.hdr.type = GPU_CMD_TRANSFER_TO_HOST_2D;
    transfer.rect.x = rect->x; transfer.rect.y = rect->y;
    transfer.rect.width = rect->width; transfer.rect.height = rect->height;
    transfer.offset = offset;
    transfer.resource_id = resource_id;
    return send_simple(&transfer.hdr, sizeof(transfer));
}

static int flush_rect_to_scanout(const gpu_rect_t *rect, uint32_t resource_id) {
    gpu_resource_flush_t flush;
    if (rect == NULL) return 0;
    bytes_zero(&flush, sizeof(flush));
    flush.hdr.type = GPU_CMD_RESOURCE_FLUSH;
    flush.rect.x = rect->x; flush.rect.y = rect->y;
    flush.rect.width = rect->width; flush.rect.height = rect->height;
    flush.resource_id = resource_id;
    return send_simple(&flush.hdr, sizeof(flush));
}

static int gop_blit_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t row, col;
    if (!gop_backend || gop_framebuffer == NULL || !present_rect_valid(x, y, width, height)) return 0;
    for (row = 0u; row < height; ++row) {
        const uint32_t *src = framebuffer + ((size_t)y + row) * active_surface.stride_pixels + x;
        uint64_t target_offset = ((uint64_t)y + row) * gop_stride * 4u + (uint64_t)x * 4u;
        if (target_offset > gop_framebuffer_size || (uint64_t)width * 4u > gop_framebuffer_size - target_offset)
            return 0;
        volatile uint8_t *dst = gop_framebuffer + target_offset;
        for (col = 0u; col < width; ++col) {
            uint32_t pixel = src[col];
            uint8_t r = (uint8_t)(pixel >> 16), g = (uint8_t)(pixel >> 8), b = (uint8_t)pixel;
            if (gop_format == CHOS_GOP_PIXEL_BGRX8) {
                dst[col * 4u] = b; dst[col * 4u + 1u] = g; dst[col * 4u + 2u] = r; dst[col * 4u + 3u] = 0xffu;
            } else if (gop_format == CHOS_GOP_PIXEL_RGBX8) {
                dst[col * 4u] = r; dst[col * 4u + 1u] = g; dst[col * 4u + 2u] = b; dst[col * 4u + 3u] = 0xffu;
            } else {
                uint32_t word = gop_pack_channel(r, gop_red_mask) | gop_pack_channel(g, gop_green_mask) |
                                gop_pack_channel(b, gop_blue_mask) | gop_reserved_mask;
                dst[col * 4u] = (uint8_t)word; dst[col * 4u + 1u] = (uint8_t)(word >> 8);
                dst[col * 4u + 2u] = (uint8_t)(word >> 16); dst[col * 4u + 3u] = (uint8_t)(word >> 24);
            }
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
    last_error = "ok";
    return 1;
}

int virtio_gpu_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    gpu_rect_t rect;
    uint32_t resource_id;
    if (!present_rect_valid(x, y, width, height)) {
        last_error = "invalid GPU present rectangle";
        return 0;
    }
    if (gop_backend) return gop_blit_rect(x, y, width, height);
    rect.x = x; rect.y = y; rect.width = width; rect.height = height;
    resource_id = GPU_RESOURCE_ID_BASE + (uint32_t)active_mode_index;
    if (!transfer_rect_to_host(&rect, resource_id)) return 0;
    return flush_rect_to_scanout(&rect, resource_id);
}

int virtio_gpu_present_rects(const chos_gpu_rect_t *rects, size_t count) {
    gpu_rect_t current, bounds;
    uint32_t resource_id;
    size_t i;
    uint32_t right, bottom;
    if (rects == NULL || count == 0u || count > CHOS_GPU_MAX_DIRTY_RECTS) {
        last_error = "invalid GPU dirty rectangle list";
        return 0;
    }
    for (i = 0u; i < count; ++i) {
        if (!present_rect_valid(rects[i].x, rects[i].y, rects[i].width, rects[i].height)) {
            last_error = "invalid GPU present rectangle";
            return 0;
        }
    }
    if (gop_backend) {
        for (i = 0u; i < count; ++i)
            if (!virtio_gpu_present_rect(rects[i].x, rects[i].y, rects[i].width, rects[i].height)) return 0;
        return 1;
    }
    bounds.x = rects[0].x; bounds.y = rects[0].y;
    bounds.width = rects[0].width; bounds.height = rects[0].height;
    right = bounds.x + bounds.width; bottom = bounds.y + bounds.height;
    resource_id = GPU_RESOURCE_ID_BASE + (uint32_t)active_mode_index;
    for (i = 0u; i < count; ++i) {
        current.x = rects[i].x; current.y = rects[i].y;
        current.width = rects[i].width; current.height = rects[i].height;
        if (!transfer_rect_to_host(&current, resource_id)) return 0;
        if (current.x < bounds.x) bounds.x = current.x;
        if (current.y < bounds.y) bounds.y = current.y;
        if (current.x + current.width > right) right = current.x + current.width;
        if (current.y + current.height > bottom) bottom = current.y + current.height;
    }
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
    return flush_rect_to_scanout(&bounds, resource_id);
}

int virtio_gpu_present_rect_pair(uint32_t x1, uint32_t y1, uint32_t width1, uint32_t height1,
                                 uint32_t x2, uint32_t y2, uint32_t width2, uint32_t height2) {
    const chos_gpu_rect_t first = {x1, y1, width1, height1};
    const chos_gpu_rect_t second = {x2, y2, width2, height2};
    const chos_gpu_rect_t rects[2] = { first, second };
    if (!present_rect_valid(x1, y1, width1, height1) ||
        !present_rect_valid(x2, y2, width2, height2)) {
        last_error = "invalid GPU present rectangle pair";
        return 0;
    }
    /* Nearby damage is cheaper as one scanout flush. When a fast drag moves
     * the outline a long distance, flushing the large empty gap dominates the
     * cost, so update each narrow strip independently. */
    if (chos_gpu_rect_pair_should_coalesce(first, second))
        return virtio_gpu_present_rects(rects, 2u);
    if (!virtio_gpu_present_rect(x1, y1, width1, height1)) return 0;
    return virtio_gpu_present_rect(x2, y2, width2, height2);
}

int virtio_gpu_present(void) {
    return virtio_gpu_present_rect(0u, 0u,
                                   (uint32_t)active_surface.width,
                                   (uint32_t)active_surface.height);
}

int virtio_gpu_ready(void) { return driver_ready && active_mode_index < CHOS_DISPLAY_MODE_COUNT && resource_created[active_mode_index]; }
const char *virtio_gpu_last_error(void) { return last_error; }
uintptr_t virtio_gpu_detected_base(void) { return detected_base; }


int virtio_gpu_cursor_available(void) {
    return driver_ready && cursor_queue_ready && cursor_resource_created && cursor_shape_loaded;
}

int virtio_gpu_cursor_init(const uint32_t *argb_pixels, uint32_t width, uint32_t height,
                           uint32_t hot_x, uint32_t hot_y) {
    gpu_resource_create_2d_t create;
    gpu_attach_backing_t attach;
    gpu_transfer_to_host_t transfer;
    size_t i;
    if (!driver_ready || !cursor_queue_ready || argb_pixels == NULL || width == 0u || height == 0u ||
        width > GPU_CURSOR_SIZE || height > GPU_CURSOR_SIZE || hot_x >= width || hot_y >= height) {
        last_error = "hardware cursor unsupported or invalid cursor asset";
        return 0;
    }
    for (i = 0u; i < GPU_CURSOR_SIZE * GPU_CURSOR_SIZE; ++i) cursor_framebuffer[i] = 0u;
    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            /* PNG conversion emits 0xAARRGGBB, matching B8G8R8A8 byte order
             * in little-endian guest memory. Preserve alpha for transparent edges. */
            cursor_framebuffer[(size_t)y * GPU_CURSOR_SIZE + x] = argb_pixels[(size_t)y * width + x];
        }
    }
    clean_range_to_poc(cursor_framebuffer, sizeof(cursor_framebuffer));

    if (!cursor_resource_created) {
        bytes_zero(&create, sizeof(create));
        create.hdr.type = GPU_CMD_RESOURCE_CREATE_2D;
        create.hdr.flags = GPU_FLAG_FENCE;
        create.hdr.fence_id = ++cursor_fence_id;
        create.resource_id = GPU_CURSOR_RESOURCE_ID;
        create.format = GPU_FORMAT_B8G8R8A8_UNORM;
        create.width = GPU_CURSOR_SIZE;
        create.height = GPU_CURSOR_SIZE;
        if (!send_simple(&create.hdr, sizeof(create))) return 0;

        bytes_zero(&attach, sizeof(attach));
        attach.hdr.type = GPU_CMD_RESOURCE_ATTACH_BACKING;
        attach.hdr.flags = GPU_FLAG_FENCE;
        attach.hdr.fence_id = ++cursor_fence_id;
        attach.resource_id = GPU_CURSOR_RESOURCE_ID;
        attach.nr_entries = 1u;
        attach.addr = (uint64_t)(uintptr_t)cursor_framebuffer;
        attach.length = (uint32_t)sizeof(cursor_framebuffer);
        if (!send_simple(&attach.hdr, sizeof(attach))) return 0;
        cursor_resource_created = 1;
    }
    bytes_zero(&transfer, sizeof(transfer));
    transfer.hdr.type = GPU_CMD_TRANSFER_TO_HOST_2D;
    transfer.hdr.flags = GPU_FLAG_FENCE;
    transfer.hdr.fence_id = ++cursor_fence_id;
    transfer.rect.x = 0u; transfer.rect.y = 0u;
    transfer.rect.width = GPU_CURSOR_SIZE; transfer.rect.height = GPU_CURSOR_SIZE;
    transfer.offset = 0u;
    transfer.resource_id = GPU_CURSOR_RESOURCE_ID;
    if (!send_simple(&transfer.hdr, sizeof(transfer))) return 0;

    cursor_hot_x = hot_x;
    cursor_hot_y = hot_y;
    cursor_shape_loaded = 1;
    last_error = "ok";
    return 1;
}

int virtio_gpu_cursor_move(int32_t x, int32_t y, int visible) {
    gpu_cursor_command_t command;
    if (!virtio_gpu_cursor_available()) return 0;
    bytes_zero(&command, sizeof(command));
    command.hdr.type = (visible && !cursor_is_visible) ? GPU_CMD_UPDATE_CURSOR : GPU_CMD_MOVE_CURSOR;
    command.pos.scanout_id = 0u;
    if (!visible) {
        command.hdr.type = GPU_CMD_UPDATE_CURSOR;
        command.resource_id = 0u; /* zero resource hides the host cursor */
        command.pos.x = 0u;
        command.pos.y = 0u;
    } else {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if ((uint32_t)x >= active_surface.width) x = (int32_t)active_surface.width - 1;
        if ((uint32_t)y >= active_surface.height) y = (int32_t)active_surface.height - 1;
        command.pos.x = (uint32_t)x;
        command.pos.y = (uint32_t)y;
        if (command.hdr.type == GPU_CMD_UPDATE_CURSOR) {
            command.resource_id = GPU_CURSOR_RESOURCE_ID;
            command.hot_x = cursor_hot_x;
            command.hot_y = cursor_hot_y;
        }
    }
    if (!send_cursor_command(&command)) return 0;
    cursor_is_visible = visible != 0;
    return 1;
}
