#include <stddef.h>
#include <stdint.h>
#include "chos/platform.h"
#include "chos/virtio_input.h"
#include "chos/gui.h"

#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_DEVICE_INPUT 18u
#define VIRTIO_F_VERSION_1 (1u << 0)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u
#define VIRTQ_DESC_F_WRITE 2u
#define VIRTIO_MMIO_SLOT_SIZE 0x200u
#define VIRTIO_MMIO_SLOT_COUNT 32u
#define INPUT_VQ_SIZE 8u
#define INPUT_DEVICE_LIMIT 4u
#define INPUT_EV_SYN 0u
#define INPUT_EV_KEY 1u
#define INPUT_EV_REL 2u
#define INPUT_EV_ABS 3u
#define INPUT_BTN_LEFT 0x110u
#define INPUT_REL_X 0u
#define INPUT_REL_Y 1u
#define INPUT_REL_HWHEEL 6u
#define INPUT_REL_WHEEL 8u
#define INPUT_ABS_X 0u
#define INPUT_ABS_Y 1u

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
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} input_desc_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[INPUT_VQ_SIZE];
    volatile uint16_t used_event;
} __attribute__((aligned(2))) input_avail_t;

typedef struct {
    volatile uint32_t id;
    volatile uint32_t len;
} __attribute__((packed)) input_used_elem_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile input_used_elem_t ring[INPUT_VQ_SIZE];
    volatile uint16_t avail_event;
} __attribute__((aligned(4))) input_used_t;

typedef struct __attribute__((aligned(16))) {
    input_desc_t desc[INPUT_VQ_SIZE] __attribute__((aligned(16)));
    input_avail_t avail __attribute__((aligned(2)));
    input_used_t used __attribute__((aligned(4)));
    uint16_t size;
} input_queue_t;

typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} input_event_t;

_Static_assert(sizeof(input_event_t) == 8u, "VirtIO-input event ABI");
_Static_assert(sizeof(input_desc_t) == 16u, "Virtqueue descriptor ABI");

typedef struct __attribute__((aligned(4096))) {
    uintptr_t base;
    int ready;
    uint16_t event_avail_idx;
    uint16_t event_last_used;
    input_queue_t event_queue;
    input_queue_t status_queue;
    input_event_t event_buffer[INPUT_VQ_SIZE] __attribute__((aligned(16)));
} input_device_t;

static input_device_t devices[INPUT_DEVICE_LIMIT];
static unsigned device_count;
static const char *last_error = "probe not run";

static volatile uint32_t *reg32(uintptr_t base, uint32_t offset) { return (volatile uint32_t *)(base + offset); }
static uint32_t read_reg(uintptr_t base, uint32_t offset) { return *reg32(base, offset); }
static void write_reg(uintptr_t base, uint32_t offset, uint32_t value) { *reg32(base, offset) = value; }
static void dma_store_barrier(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void dma_load_barrier(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static void cpu_full_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void set_status(uintptr_t base, uint32_t value) { write_reg(base, REG_STATUS, value); cpu_full_barrier(); }
static void bytes_zero(void *destination, size_t count) {
    uint8_t *d = (uint8_t *)destination;
    size_t i;
    for (i = 0u; i < count; ++i) d[i] = 0u;
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
    for (address = start; address < end; address += line_size) __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
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
    for (address = start; address < end; address += line_size) __asm__ volatile("dc ivac, %0" :: "r"(address) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static void queue_reset(input_queue_t *queue) {
    uint16_t i;
    bytes_zero(queue->desc, sizeof(queue->desc));
    bytes_zero((void *)&queue->avail, sizeof(queue->avail));
    bytes_zero((void *)&queue->used, sizeof(queue->used));
    queue->size = 0u;
    for (i = 0u; i < INPUT_VQ_SIZE; ++i) queue->avail.ring[i] = 0u;
}

static int queue_configure(uintptr_t base, uint16_t index, input_queue_t *queue, int post_event_buffers,
                           input_event_t *event_buffer, uint16_t *available_idx_out) {
    uint32_t max_queue;
    uint16_t size, i;
    write_reg(base, REG_QUEUE_SEL, index);
    max_queue = read_reg(base, REG_QUEUE_NUM_MAX);
    if (max_queue < 2u) return 0;
    size = INPUT_VQ_SIZE;
    while (size > max_queue) size >>= 1;
    if (size < 2u) return 0;
    queue_reset(queue);
    queue->size = size;
    if (post_event_buffers) {
        for (i = 0u; i < size; ++i) {
            queue->desc[i].addr = (uint64_t)(uintptr_t)&event_buffer[i];
            queue->desc[i].len = (uint32_t)sizeof(event_buffer[i]);
            queue->desc[i].flags = VIRTQ_DESC_F_WRITE;
            queue->desc[i].next = 0u;
            queue->avail.ring[i] = i;
        }
        queue->avail.idx = size;
        *available_idx_out = size;
        clean_range_to_poc(event_buffer, (size_t)size * sizeof(event_buffer[0]));
    }
    clean_range_to_poc(queue->desc, sizeof(queue->desc));
    clean_range_to_poc((const void *)&queue->avail, sizeof(queue->avail));
    write_reg(base, REG_QUEUE_NUM, size);
    write_reg(base, REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)queue->desc);
    write_reg(base, REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)queue->desc >> 32));
    write_reg(base, REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)&queue->avail);
    write_reg(base, REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)&queue->avail >> 32));
    write_reg(base, REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)&queue->used);
    write_reg(base, REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)&queue->used >> 32));
    write_reg(base, REG_QUEUE_READY, 1u);
    cpu_full_barrier();
    return 1;
}

static int initialize_device(input_device_t *device, uintptr_t base) {
    uint32_t features0, features1, status;
    device->base = base;
    device->ready = 0;
    device->event_avail_idx = 0u;
    device->event_last_used = 0u;
    bytes_zero(device->event_buffer, sizeof(device->event_buffer));
    set_status(base, 0u);
    set_status(base, VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(base, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    write_reg(base, REG_DEVICE_FEATURES_SEL, 0u); features0 = read_reg(base, REG_DEVICE_FEATURES);
    write_reg(base, REG_DEVICE_FEATURES_SEL, 1u); features1 = read_reg(base, REG_DEVICE_FEATURES);
    (void)features0;
    if ((features1 & VIRTIO_F_VERSION_1) == 0u) { set_status(base, VIRTIO_STATUS_FAILED); last_error = "VirtIO input does not offer VERSION_1"; return 0; }
    write_reg(base, REG_DRIVER_FEATURES_SEL, 0u); write_reg(base, REG_DRIVER_FEATURES, 0u);
    write_reg(base, REG_DRIVER_FEATURES_SEL, 1u); write_reg(base, REG_DRIVER_FEATURES, VIRTIO_F_VERSION_1);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    set_status(base, status);
    if ((read_reg(base, REG_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        set_status(base, VIRTIO_STATUS_FAILED); last_error = "VirtIO input rejected negotiated features"; return 0;
    }
    if (!queue_configure(base, 0u, &device->event_queue, 1, device->event_buffer, &device->event_avail_idx) ||
        !queue_configure(base, 1u, &device->status_queue, 0, device->event_buffer, &device->event_avail_idx)) {
        set_status(base, VIRTIO_STATUS_FAILED); last_error = "VirtIO input event/status queues unavailable"; return 0;
    }
    set_status(base, status | VIRTIO_STATUS_DRIVER_OK);
    device->ready = 1;
    write_reg(base, REG_QUEUE_NOTIFY, 0u);
    last_error = "ok";
    return 1;
}

int chos_input_init(void) {
    uintptr_t first_base = platform_virtio_mmio_base();
    uint32_t slot;
    device_count = 0u;
    last_error = "no modern VirtIO input devices found";
    if (first_base == 0u) { last_error = "platform has no VirtIO-MMIO aperture"; return 0; }
    bytes_zero(devices, sizeof(devices));
    for (slot = 0u; slot < VIRTIO_MMIO_SLOT_COUNT && device_count < INPUT_DEVICE_LIMIT; ++slot) {
        uintptr_t base = first_base + (uintptr_t)slot * VIRTIO_MMIO_SLOT_SIZE;
        if (read_reg(base, REG_MAGIC) != VIRTIO_MAGIC || read_reg(base, REG_DEVICE_ID) != VIRTIO_DEVICE_INPUT) continue;
        if (read_reg(base, REG_VERSION) < 2u) { last_error = "legacy VirtIO input is unsupported"; continue; }
        if (initialize_device(&devices[device_count], base)) ++device_count;
    }
    if (device_count == 0u) {
        return 0;
    }
    return (int)device_count;
}

static void handle_event(const input_event_t *event) {
    if (event->type == INPUT_EV_KEY) {
        if (event->code >= INPUT_BTN_LEFT) {
            chos_gui_input_button(event->code, event->value != 0u);
        } else {
            chos_gui_input_key(event->code, event->value != 0u);
        }
    } else if (event->type == INPUT_EV_REL) {
        if (event->code == INPUT_REL_X) chos_gui_input_pointer_relative((int32_t)event->value, 0);
        else if (event->code == INPUT_REL_Y) chos_gui_input_pointer_relative(0, (int32_t)event->value);
        else if (event->code == INPUT_REL_WHEEL) chos_gui_input_scroll(-(int32_t)event->value);
        else if (event->code == INPUT_REL_HWHEEL) { /* reserved for horizontal scrolling */ }
    } else if (event->type == INPUT_EV_ABS) {
        if (event->code == INPUT_ABS_X) chos_gui_input_pointer_absolute((int32_t)event->value, -1);
        else if (event->code == INPUT_ABS_Y) chos_gui_input_pointer_absolute(-1, (int32_t)event->value);
    } else if (event->type == INPUT_EV_SYN) {
        /* SYN_REPORT is useful to device consumers, but the current UI redraw is coalesced in gui_pump. */
    }
}

void chos_input_poll(void) {
    unsigned i;
    for (i = 0u; i < device_count; ++i) {
        input_device_t *d = &devices[i];
        input_queue_t *q;
        uint16_t end;
        if (!d->ready) continue;
        q = &d->event_queue;
        invalidate_range_from_poc((const void *)&q->used, sizeof(q->used));
        dma_load_barrier();
        end = q->used.idx;
        while (d->event_last_used != end) {
            input_used_elem_t used;
            uint16_t ring_index = (uint16_t)(d->event_last_used & (q->size - 1u));
            used.id = q->used.ring[ring_index].id;
            used.len = q->used.ring[ring_index].len;
            ++d->event_last_used;
            if (used.id < q->size && used.len >= sizeof(input_event_t)) {
                input_event_t *event = &d->event_buffer[used.id];
                invalidate_range_from_poc(event, sizeof(*event));
                handle_event(event);
                q->avail.ring[d->event_avail_idx & (q->size - 1u)] = (uint16_t)used.id;
                ++d->event_avail_idx;
                dma_store_barrier();
                q->avail.idx = d->event_avail_idx;
                clean_range_to_poc((const void *)&q->avail, sizeof(q->avail));
                write_reg(d->base, REG_QUEUE_NOTIFY, 0u);
            } else {
                d->ready = 0;
                last_error = "VirtIO input returned an invalid event buffer";
                break;
            }
        }
    }
    /* Pump even without new input: EL0 writes and timer/process state changes can
       mark the GUI dirty independently of keyboard or pointer activity. The GUI
       pump itself is cheap when clean and only presents a frame when necessary. */
    chos_gui_pump();
}

unsigned chos_input_device_count(void) { return device_count; }
const char *chos_input_last_error(void) { return last_error; }
