#include <stddef.h>
#include <stdint.h>
#include "chos/platform.h"
#include "chos/virtio_transport.h"
#include "chos/virtio_net.h"
#include "chos/virtio_net_abi.h"

#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_DEVICE_NET 1u
#define VIRTIO_F_VERSION_1 (1u << 0) /* feature word 1 */
#define VIRTIO_NET_F_MAC (1u << 5)
#define VIRTIO_NET_F_STATUS (1u << 16)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u
#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u
#define VIRTIO_MMIO_SLOT_SIZE 0x200u
#define VIRTIO_MMIO_SLOT_COUNT 32u
#define NET_QUEUE_CAPACITY 8u
#define NET_HEADER_SIZE CHOS_VIRTIO_NET_HEADER_V1_SIZE
#define NET_FRAME_CAPACITY CHOS_NET_MAX_ETHERNET_FRAME
#define NET_RX_BUFFER_SIZE (NET_HEADER_SIZE + NET_FRAME_CAPACITY)
#define NET_TX_WAIT_LIMIT 20000000u
#define NET_RX_QUEUE 0u
#define NET_TX_QUEUE 1u

/* VirtIO-MMIO v2 register offsets. */
enum {
    REG_MAGIC=0x000, REG_VERSION=0x004, REG_DEVICE_ID=0x008,
    REG_DEVICE_FEATURES=0x010, REG_DEVICE_FEATURES_SEL=0x014,
    REG_DRIVER_FEATURES=0x020, REG_DRIVER_FEATURES_SEL=0x024,
    REG_QUEUE_SEL=0x030, REG_QUEUE_NUM_MAX=0x034, REG_QUEUE_NUM=0x038,
    REG_QUEUE_READY=0x044, REG_QUEUE_NOTIFY=0x050,
    REG_INTERRUPT_STATUS=0x060, REG_STATUS=0x070, REG_QUEUE_DESC_LOW=0x080, REG_QUEUE_DESC_HIGH=0x084,
    REG_QUEUE_DRIVER_LOW=0x090, REG_QUEUE_DRIVER_HIGH=0x094,
    REG_QUEUE_DEVICE_LOW=0x0a0, REG_QUEUE_DEVICE_HIGH=0x0a4,
    REG_CONFIG=0x100
};

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} net_virtq_desc_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[NET_QUEUE_CAPACITY];
    volatile uint16_t used_event;
} __attribute__((aligned(2))) net_virtq_avail_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile struct { uint32_t id; uint32_t len; } ring[NET_QUEUE_CAPACITY];
    volatile uint16_t avail_event;
} __attribute__((aligned(4))) net_virtq_used_t;

/* Modern VirtIO 1.x transport requires the 12-byte virtio_net_hdr_v1. */
typedef chos_virtio_net_header_v1_t net_header_t;

_Static_assert(sizeof(net_virtq_desc_t) == 16u, "VirtIO net descriptor ABI");
_Static_assert(sizeof(net_header_t) == NET_HEADER_SIZE, "VirtIO net header ABI");

static net_virtq_desc_t rx_desc[NET_QUEUE_CAPACITY] __attribute__((aligned(16)));
static net_virtq_avail_t rx_avail __attribute__((aligned(2)));
static net_virtq_used_t rx_used __attribute__((aligned(4)));
static uint8_t rx_buffers[NET_QUEUE_CAPACITY][NET_RX_BUFFER_SIZE] __attribute__((aligned(16)));
static net_virtq_desc_t tx_desc[NET_QUEUE_CAPACITY] __attribute__((aligned(16)));
static net_virtq_avail_t tx_avail __attribute__((aligned(2)));
static net_virtq_used_t tx_used __attribute__((aligned(4)));
static net_header_t tx_header __attribute__((aligned(16)));
static uint8_t tx_buffer[NET_FRAME_CAPACITY] __attribute__((aligned(16)));
static uintptr_t mmio_base;
static uint16_t queue_size;
static uint16_t tx_available_idx;
static uint16_t tx_last_used_idx;
static uint16_t rx_available_idx;
static uint16_t rx_last_used_idx;
static int driver_ready;
static int tx_busy;
static chos_virtio_net_info_t net_info;
static const char *last_error = "probe not run";

static uint32_t read_reg(uint32_t offset) { return chos_virtio_read(mmio_base,offset); }
static void write_reg(uint32_t offset, uint32_t value) { chos_virtio_write(mmio_base,offset,value); }
static void dma_store_barrier(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void dma_load_barrier(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static void cpu_full_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void set_status(uint32_t value) { write_reg(REG_STATUS, value); cpu_full_barrier(); }
static void zero_bytes(void *destination, size_t size) {
    uint8_t *out = (uint8_t *)destination;
    size_t i;
    for (i = 0u; i < size; ++i) out[i] = 0u;
}
static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t i;
    for (i = 0u; i < size; ++i) out[i] = in[i];
}
static int configure_queue(uint16_t index, net_virtq_desc_t *desc,
                           net_virtq_avail_t *avail, net_virtq_used_t *used) {
    uint32_t maximum;
    write_reg(REG_QUEUE_SEL, index);
    maximum = read_reg(REG_QUEUE_NUM_MAX);
    if (maximum < queue_size || queue_size < 2u) return 0;
    write_reg(REG_QUEUE_READY, 0u);
    write_reg(REG_QUEUE_NUM, queue_size);
    write_reg(REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)desc);
    write_reg(REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)desc >> 32));
    write_reg(REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)avail);
    write_reg(REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)avail >> 32));
    write_reg(REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)used);
    write_reg(REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)used >> 32));
    dma_store_barrier();
    write_reg(REG_QUEUE_READY, 1u);
    cpu_full_barrier();
    return 1;
}
static void reset_state(void) {
    driver_ready = 0;
    tx_busy = 0;
    mmio_base = 0u;
    queue_size = NET_QUEUE_CAPACITY;
    tx_available_idx = tx_last_used_idx = 0u;
    rx_available_idx = rx_last_used_idx = 0u;
    zero_bytes(&net_info, sizeof(net_info));
}
static void mark_failed(const char *reason) {
    if (mmio_base != 0u) set_status(read_reg(REG_STATUS) | VIRTIO_STATUS_FAILED);
    driver_ready = 0;
    net_info.ready = 0;
    last_error = reason;
}

int virtio_net_init(void) {
    uintptr_t first_base = chos_virtio_aperture();
    uint32_t slot, features0, features1, driver0, driver1, status;
    uint32_t mac_word0, mac_word1, device_status, rx_max, tx_max, queue_max;
    uint16_t i;
    int found = 0;
    reset_state();
    last_error = "no modern VirtIO network device found";
    if (first_base == 0u) { last_error = "platform has no VirtIO-MMIO aperture"; return 0; }
    for (slot = 0u; slot < VIRTIO_MMIO_SLOT_COUNT; ++slot) {
        uintptr_t candidate = first_base + (uintptr_t)slot * VIRTIO_MMIO_SLOT_SIZE;
        if (chos_virtio_read(candidate,REG_MAGIC)!=VIRTIO_MAGIC || chos_virtio_read(candidate,REG_DEVICE_ID)!=VIRTIO_DEVICE_NET)continue;
        if (chos_virtio_read(candidate,REG_VERSION)<2u) { last_error = "legacy VirtIO-net MMIO transport is unsupported"; continue; }
        mmio_base = candidate;
        found = 1;
        break;
    }
    if (!found) return 0;

    set_status(0u);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    write_reg(REG_DEVICE_FEATURES_SEL, 0u); features0 = read_reg(REG_DEVICE_FEATURES);
    write_reg(REG_DEVICE_FEATURES_SEL, 1u); features1 = read_reg(REG_DEVICE_FEATURES);
    if ((features1 & VIRTIO_F_VERSION_1) == 0u) {
        mark_failed("VirtIO-net does not offer VIRTIO_F_VERSION_1"); return 0;
    }
    driver0 = features0 & (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    driver1 = VIRTIO_F_VERSION_1;
    write_reg(REG_DRIVER_FEATURES_SEL, 0u); write_reg(REG_DRIVER_FEATURES, driver0);
    write_reg(REG_DRIVER_FEATURES_SEL, 1u); write_reg(REG_DRIVER_FEATURES, driver1);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    set_status(status);
    if ((read_reg(REG_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        mark_failed("VirtIO-net rejected negotiated features"); return 0;
    }

    mac_word0 = read_reg(REG_CONFIG);
    mac_word1 = read_reg(REG_CONFIG + 4u);
    net_info.mac[0] = (uint8_t)mac_word0;
    net_info.mac[1] = (uint8_t)(mac_word0 >> 8);
    net_info.mac[2] = (uint8_t)(mac_word0 >> 16);
    net_info.mac[3] = (uint8_t)(mac_word0 >> 24);
    net_info.mac[4] = (uint8_t)mac_word1;
    net_info.mac[5] = (uint8_t)(mac_word1 >> 8);
    net_info.mmio_base = mmio_base;
    net_info.link_status_known = (driver0 & VIRTIO_NET_F_STATUS) != 0u;
    device_status = net_info.link_status_known ? ((mac_word1 >> 16) & 0xffffu) : 1u;
    net_info.device_status = (uint16_t)device_status;
    net_info.link_up = (device_status & 1u) != 0u;

    zero_bytes(rx_desc, sizeof(rx_desc)); zero_bytes(&rx_avail, sizeof(rx_avail));
    zero_bytes(&rx_used, sizeof(rx_used)); zero_bytes(rx_buffers, sizeof(rx_buffers));
    zero_bytes(tx_desc, sizeof(tx_desc)); zero_bytes(&tx_avail, sizeof(tx_avail));
    zero_bytes(&tx_used, sizeof(tx_used)); zero_bytes(&tx_header, sizeof(tx_header));
    zero_bytes(tx_buffer, sizeof(tx_buffer));
    queue_size = NET_QUEUE_CAPACITY;
    /* Use the same size for both network queues. */
    write_reg(REG_QUEUE_SEL, NET_RX_QUEUE); rx_max = read_reg(REG_QUEUE_NUM_MAX);
    write_reg(REG_QUEUE_SEL, NET_TX_QUEUE); tx_max = read_reg(REG_QUEUE_NUM_MAX);
    if (rx_max < 2u || tx_max < 2u) { mark_failed("VirtIO-net RX/TX queue is unavailable"); return 0; }
    queue_max = rx_max < tx_max ? rx_max : tx_max;
    queue_size = NET_QUEUE_CAPACITY;
    while (queue_size > queue_max) queue_size >>= 1;
    if (queue_size < 2u) { mark_failed("VirtIO-net queues are too small"); return 0; }

    /* Configure RX first and post writable buffers before DRIVER_OK. */
    for (i = 0u; i < queue_size; ++i) {
        rx_desc[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_desc[i].len = NET_RX_BUFFER_SIZE;
        rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
        rx_desc[i].next = 0u;
        rx_avail.ring[i] = i;
    }
    rx_available_idx = queue_size;
    rx_avail.idx = rx_available_idx;
    if (!configure_queue(NET_RX_QUEUE, rx_desc, &rx_avail, &rx_used)) {
        mark_failed("VirtIO-net RX queue setup failed"); return 0;
    }
    if (!configure_queue(NET_TX_QUEUE, tx_desc, &tx_avail, &tx_used)) {
        mark_failed("VirtIO-net TX queue setup failed"); return 0;
    }
    set_status(status | VIRTIO_STATUS_DRIVER_OK);
    driver_ready = 1;
    net_info.ready = 1;
    dma_store_barrier();
    write_reg(REG_QUEUE_NOTIFY, NET_RX_QUEUE);
    cpu_full_barrier();
    last_error = "ok";
    return 1;
}

int virtio_net_ready(void) { return driver_ready; }
const char *virtio_net_last_error(void) { return last_error; }
void virtio_net_get_info(chos_virtio_net_info_t *out) {
    if (out == NULL) return;
    if (driver_ready && net_info.link_status_known) {
        uint32_t config_status = (read_reg(REG_CONFIG + 4u) >> 16) & 0xffffu;
        net_info.device_status = (uint16_t)config_status;
        net_info.link_up = (config_status & 1u) != 0u;
    }
    /* Avoid exposing internal mutable state by value-copying the public snapshot. */
    copy_bytes(out, &net_info, sizeof(*out));
    if (driver_ready) {
        /* Raw queue indices distinguish a host that never delivered a frame
         * from one that completed RX descriptors the parser later rejected. */
        write_reg(REG_QUEUE_SEL, NET_RX_QUEUE);
        /* QueueNum readback is zero on some QEMU builds after QUEUE_READY; report
         * the validated size configured by the driver, not that readback. */
        out->rx_queue_size = queue_size;
        out->rx_queue_num_readback = (uint16_t)read_reg(REG_QUEUE_NUM);
        out->rx_queue_ready = read_reg(REG_QUEUE_READY);
        out->rx_available_idx = rx_avail.idx;
        dma_load_barrier();
        out->rx_device_used_idx = rx_used.idx;
        out->rx_consumed_idx = rx_last_used_idx;
        out->interrupt_status = read_reg(REG_INTERRUPT_STATUS);
    }
}

int virtio_net_send_frame(const void *frame, size_t length) {
    uint16_t used_before;
    uint32_t spin;
    if (!driver_ready || frame == NULL || length < 14u || length > NET_FRAME_CAPACITY) return 0;
    if (tx_busy) { ++net_info.tx_errors; last_error = "VirtIO-net TX queue is busy"; return 0; }
    tx_busy = 1;
    zero_bytes(&tx_header, sizeof(tx_header));
    copy_bytes(tx_buffer, frame, length);
    tx_desc[0].addr = (uint64_t)(uintptr_t)&tx_header;
    tx_desc[0].len = sizeof(tx_header);
    tx_desc[0].flags = VIRTQ_DESC_F_NEXT;
    tx_desc[0].next = 1u;
    tx_desc[1].addr = (uint64_t)(uintptr_t)tx_buffer;
    tx_desc[1].len = (uint32_t)length;
    tx_desc[1].flags = 0u;
    tx_desc[1].next = 0u;
    used_before = tx_used.idx;
    tx_avail.ring[tx_available_idx & (queue_size - 1u)] = 0u;
    dma_store_barrier();
    ++tx_available_idx;
    tx_avail.idx = tx_available_idx;
    dma_store_barrier();
    write_reg(REG_QUEUE_NOTIFY, NET_TX_QUEUE);
    for (spin = 0u; spin < NET_TX_WAIT_LIMIT; ++spin) {
        dma_load_barrier();
        if (tx_used.idx != used_before) break;
    }
    if (spin == NET_TX_WAIT_LIMIT) {
        ++net_info.tx_errors;
        tx_busy = 0;
        mark_failed("VirtIO-net TX completion timed out; queue quarantined");
        return 0;
    }
    tx_last_used_idx = tx_used.idx;
    (void)tx_last_used_idx;
    ++net_info.tx_frames;
    tx_busy = 0;
    last_error = "ok";
    return 1;
}

int virtio_net_receive_frame(void *frame, size_t capacity, size_t *length_out) {
    uint16_t used_index;
    uint32_t id, used_length;
    size_t frame_length;
    if (length_out != NULL) *length_out = 0u;
    if (!driver_ready || frame == NULL || length_out == NULL) return -1;
    dma_load_barrier();
    if (rx_used.idx == rx_last_used_idx) return 0;
    used_index = (uint16_t)(rx_last_used_idx & (queue_size - 1u));
    id = rx_used.ring[used_index].id;
    used_length = rx_used.ring[used_index].len;
    ++rx_last_used_idx;
    if (id >= queue_size) {
        mark_failed("VirtIO-net returned invalid RX descriptor id");
        return -1;
    }
    if (used_length < NET_HEADER_SIZE + 14u || used_length > NET_RX_BUFFER_SIZE) {
        ++net_info.rx_dropped;
    } else {
        frame_length = (size_t)used_length - NET_HEADER_SIZE;
        if (frame_length > capacity || frame_length > NET_FRAME_CAPACITY) {
            ++net_info.rx_dropped;
        } else {
            copy_bytes(frame, &rx_buffers[id][NET_HEADER_SIZE], frame_length);
            *length_out = frame_length;
            ++net_info.rx_frames;
        }
    }
    /* Return the same buffer to the device, including on a rejected frame. */
    rx_desc[id].addr = (uint64_t)(uintptr_t)rx_buffers[id];
    rx_desc[id].len = NET_RX_BUFFER_SIZE;
    rx_desc[id].flags = VIRTQ_DESC_F_WRITE;
    rx_desc[id].next = 0u;
    rx_avail.ring[rx_available_idx & (queue_size - 1u)] = (uint16_t)id;
    dma_store_barrier();
    ++rx_available_idx;
    rx_avail.idx = rx_available_idx;
    dma_store_barrier();
    write_reg(REG_QUEUE_NOTIFY, NET_RX_QUEUE);
    cpu_full_barrier();
    if (*length_out != 0u) { last_error = "ok"; return 1; }
    return 0;
}
