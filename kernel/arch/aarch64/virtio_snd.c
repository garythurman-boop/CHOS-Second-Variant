#include <stddef.h>
#include <stdint.h>
#include "chos/audio.h"
#include "chos/platform.h"

#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_DEVICE_SOUND 25u
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
#define SND_QUEUE_COUNT 4u
#define SND_QUEUE_SIZE 8u
#define SND_WAIT_LIMIT 20000000u
#define SND_CONFIG 0x100u
#define SND_R_PCM_INFO 0x0100u
#define SND_R_PCM_SET_PARAMS 0x0101u
#define SND_R_PCM_PREPARE 0x0102u
#define SND_R_PCM_START 0x0104u
#define SND_S_OK 0x8000u
#define SND_PCM_FMT_S16 5u
#define SND_PCM_RATE_48000 7u
#define SND_CHIME_FRAMES 28800u
#define SND_CHIME_BYTES (SND_CHIME_FRAMES * 2u * sizeof(int16_t))

/* VirtIO-MMIO v2 register offsets. */
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
} snd_desc_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[SND_QUEUE_SIZE];
    volatile uint16_t used_event;
} __attribute__((aligned(2))) snd_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} snd_used_elem_t;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile snd_used_elem_t ring[SND_QUEUE_SIZE];
    volatile uint16_t avail_event;
} __attribute__((aligned(4))) snd_used_t;

typedef struct __attribute__((aligned(4096))) {
    snd_desc_t desc[SND_QUEUE_SIZE] __attribute__((aligned(16)));
    snd_avail_t avail __attribute__((aligned(2)));
    snd_used_t used __attribute__((aligned(4)));
    uint16_t size;
} snd_queue_t;

typedef struct __attribute__((packed)) { uint32_t code; } snd_hdr_t;
typedef struct __attribute__((packed)) {
    snd_hdr_t hdr;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
} snd_query_info_t;
typedef struct __attribute__((packed)) {
    uint32_t hda_fn_nid;
    uint32_t features;
    uint64_t formats;
    uint64_t rates;
    uint8_t direction;
    uint8_t channels_min;
    uint8_t channels_max;
    uint8_t padding[5];
} snd_pcm_info_t;
typedef struct __attribute__((packed)) {
    snd_hdr_t hdr;
    uint32_t stream_id;
} snd_pcm_hdr_t;
typedef struct __attribute__((packed)) {
    snd_pcm_hdr_t hdr;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t channels;
    uint8_t format;
    uint8_t rate;
    uint8_t padding;
} snd_pcm_set_params_t;
typedef struct __attribute__((packed)) { uint32_t stream_id; } snd_pcm_xfer_t;
typedef struct __attribute__((packed)) { uint32_t status; uint32_t latency_bytes; } snd_pcm_status_t;

_Static_assert(sizeof(snd_desc_t) == 16u, "VirtIO-Sound descriptor ABI");
_Static_assert(sizeof(snd_query_info_t) == 16u, "VirtIO-Sound query ABI");
_Static_assert(sizeof(snd_pcm_info_t) == 32u, "VirtIO-Sound PCM info ABI");
_Static_assert(sizeof(snd_pcm_set_params_t) == 24u, "VirtIO-Sound SET_PARAMS ABI");

static snd_queue_t queues[SND_QUEUE_COUNT];
static uint8_t response_buffer[128] __attribute__((aligned(16)));
static uint8_t event_buffers[SND_QUEUE_COUNT][SND_QUEUE_SIZE][8] __attribute__((aligned(16)));
static snd_hdr_t command_status __attribute__((aligned(16)));
static snd_pcm_xfer_t xfer_header __attribute__((aligned(16)));
static snd_pcm_status_t xfer_status __attribute__((aligned(16)));
static int16_t chime_samples[SND_CHIME_FRAMES * 2u] __attribute__((aligned(16)));
static uintptr_t snd_base;
static uint16_t queue_avail_index[SND_QUEUE_COUNT];
static uint16_t queue_last_used[SND_QUEUE_COUNT];
static uint32_t queue_sizes[SND_QUEUE_COUNT];
static uint32_t playback_stream_count;
static int sound_ready;
static int sound_pending;
static int sound_chime_started;
static chos_audio_chime_state_t sound_chime_state;
static const char *sound_error = "VirtIO-Sound has not been probed";

static volatile uint32_t *reg32(uint32_t offset) { return (volatile uint32_t *)(snd_base + offset); }
static uint32_t read_reg(uint32_t offset) { return *reg32(offset); }
static void write_reg(uint32_t offset, uint32_t value) { *reg32(offset) = value; }
static uint32_t read_config32(uint32_t offset) { return *(volatile uint32_t *)(snd_base + SND_CONFIG + offset); }
static void dma_store_barrier(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void dma_load_barrier(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static void cpu_full_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void set_status(uint32_t status) { write_reg(REG_STATUS, status); cpu_full_barrier(); }

static void bytes_zero(void *destination, size_t length) {
    uint8_t *p = (uint8_t *)destination;
    size_t i;
    for (i = 0u; i < length; ++i) p[i] = 0u;
}
static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *d = (uint8_t *)destination;
    const uint8_t *s = (const uint8_t *)source;
    size_t i;
    for (i = 0u; i < length; ++i) d[i] = s[i];
}
static void clean_range_to_poc(const void *memory, size_t length) {
    uint64_t ctr;
    uintptr_t line_size, address, end;
    if (memory == NULL || length == 0u) return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line_size < 4u || (line_size & (line_size - 1u)) != 0u) line_size = 64u;
    address = (uintptr_t)memory & ~(line_size - 1u);
    end = (uintptr_t)memory + length;
    for (; address < end; address += line_size) __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
static void invalidate_range_from_poc(const void *memory, size_t length) {
    uint64_t ctr;
    uintptr_t line_size, address, end;
    if (memory == NULL || length == 0u) return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line_size < 4u || (line_size & (line_size - 1u)) != 0u) line_size = 64u;
    address = (uintptr_t)memory & ~(line_size - 1u);
    end = (uintptr_t)memory + length;
    for (; address < end; address += line_size) __asm__ volatile("dc ivac, %0" :: "r"(address) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static int configure_queue(uint16_t index) {
    snd_queue_t *q = &queues[index];
    uint32_t max_size;
    uint16_t i, size;
    write_reg(REG_QUEUE_SEL, index);
    max_size = read_reg(REG_QUEUE_NUM_MAX);
    if (max_size < 2u) return 0;
    size = SND_QUEUE_SIZE;
    while (size > max_size) size >>= 1;
    if (size < 2u) return 0;
    bytes_zero(q->desc, sizeof(q->desc));
    bytes_zero((void *)&q->avail, sizeof(q->avail));
    bytes_zero((void *)&q->used, sizeof(q->used));
    q->size = size;
    queue_avail_index[index] = 0u;
    queue_last_used[index] = 0u;
    queue_sizes[index] = size;
    if (index == 1u || index == 3u) {
        for (i = 0u; i < size; ++i) {
            q->desc[i].addr = (uint64_t)(uintptr_t)&event_buffers[index][i][0];
            q->desc[i].len = sizeof(event_buffers[index][i]);
            q->desc[i].flags = VIRTQ_DESC_F_WRITE;
            q->desc[i].next = 0u;
            q->avail.ring[i] = i;
        }
        q->avail.idx = size;
        queue_avail_index[index] = size;
        clean_range_to_poc(event_buffers[index], (size_t)size * sizeof(event_buffers[index][0]));
    }
    clean_range_to_poc(q->desc, sizeof(q->desc));
    clean_range_to_poc((const void *)&q->avail, sizeof(q->avail));
    clean_range_to_poc((const void *)&q->used, sizeof(q->used));
    write_reg(REG_QUEUE_NUM, size);
    write_reg(REG_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)q->desc);
    write_reg(REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)q->desc >> 32));
    write_reg(REG_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)&q->avail);
    write_reg(REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)(uintptr_t)&q->avail >> 32));
    write_reg(REG_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)&q->used);
    write_reg(REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)(uintptr_t)&q->used >> 32));
    write_reg(REG_QUEUE_READY, 1u);
    cpu_full_barrier();
    return 1;
}

static int wait_for_used(uint16_t queue_index, uint32_t *used_id) {
    snd_queue_t *q = &queues[queue_index];
    uint32_t spin;
    for (spin = 0u; spin < SND_WAIT_LIMIT; ++spin) {
        if ((spin & 0xffu) == 0u) invalidate_range_from_poc((const void *)&q->used, sizeof(q->used));
        dma_load_barrier();
        if (q->used.idx != queue_last_used[queue_index]) break;
    }
    if (spin == SND_WAIT_LIMIT) {
        sound_error = "VirtIO-Sound queue timed out";
        return 0;
    }
    invalidate_range_from_poc((const void *)&q->used, sizeof(q->used));
    dma_load_barrier();
    *used_id = q->used.ring[queue_last_used[queue_index] & (q->size - 1u)].id;
    ++queue_last_used[queue_index];
    return 1;
}

static int submit_control(const void *request, size_t request_size, void *response,
                          size_t response_size, uint32_t expected_status) {
    snd_queue_t *q = &queues[0];
    uint32_t used_id;
    uint16_t slot;
    if (request == NULL || request_size == 0u || request_size > sizeof(response_buffer) ||
        response == NULL || response_size == 0u || response_size > sizeof(response_buffer) || q->size < 2u) return 0;
    bytes_zero(response_buffer, sizeof(response_buffer));
    bytes_copy(response_buffer, request, request_size);
    q->desc[0].addr = (uint64_t)(uintptr_t)response_buffer;
    q->desc[0].len = (uint32_t)request_size;
    q->desc[0].flags = VIRTQ_DESC_F_NEXT;
    q->desc[0].next = 1u;
    q->desc[1].addr = (uint64_t)(uintptr_t)&response_buffer[64];
    q->desc[1].len = (uint32_t)response_size;
    q->desc[1].flags = VIRTQ_DESC_F_WRITE;
    q->desc[1].next = 0u;
    clean_range_to_poc(q->desc, sizeof(q->desc));
    clean_range_to_poc(response_buffer, request_size);
    clean_range_to_poc(&response_buffer[64], response_size);
    slot = queue_avail_index[0] & (uint16_t)(q->size - 1u);
    q->avail.ring[slot] = 0u;
    dma_store_barrier();
    ++queue_avail_index[0];
    q->avail.idx = queue_avail_index[0];
    clean_range_to_poc((const void *)&q->avail, sizeof(q->avail));
    write_reg(REG_QUEUE_NOTIFY, 0u);
    if (!wait_for_used(0u, &used_id) || used_id != 0u) {
        sound_error = "VirtIO-Sound control queue completion invalid";
        return 0;
    }
    invalidate_range_from_poc(&response_buffer[64], response_size);
    bytes_copy(response, &response_buffer[64], response_size);
    if (response_size >= sizeof(uint32_t)) {
        uint32_t status;
        bytes_copy(&status, response, sizeof(status));
        if (status != expected_status) {
            sound_error = status == 0x8002u ? "VirtIO-Sound command not supported" : "VirtIO-Sound command failed";
            return 0;
        }
    }
    sound_error = "ok";
    return 1;
}

static int configure_playback(void) {
    snd_query_info_t query;
    uint8_t reply[sizeof(snd_hdr_t) + sizeof(snd_pcm_info_t)];
    snd_pcm_info_t info;
    snd_pcm_set_params_t params;
    snd_pcm_hdr_t pcm_command;
    if (playback_stream_count == 0u) { sound_error = "VirtIO-Sound reports no PCM streams"; return 0; }
    bytes_zero(&query, sizeof(query));
    query.hdr.code = SND_R_PCM_INFO;
    query.start_id = 0u;
    query.count = 1u;
    query.size = sizeof(snd_pcm_info_t);
    bytes_zero(reply, sizeof(reply));
    if (!submit_control(&query, sizeof(query), reply, sizeof(reply), SND_S_OK)) return 0;
    bytes_copy(&info, &reply[sizeof(snd_hdr_t)], sizeof(info));
    if (info.direction != 0u || info.channels_min > 2u || info.channels_max < 2u ||
        (info.formats & (1ull << SND_PCM_FMT_S16)) == 0u ||
        (info.rates & (1ull << SND_PCM_RATE_48000)) == 0u) {
        sound_error = "VirtIO-Sound playback does not support stereo S16 at 48000 Hz";
        return 0;
    }
    bytes_zero(&params, sizeof(params));
    params.hdr.hdr.code = SND_R_PCM_SET_PARAMS;
    params.hdr.stream_id = 0u;
    params.buffer_bytes = SND_CHIME_BYTES;
    /* 24 integral periods avoid a non-divisible buffer/period pair. */
    params.period_bytes = SND_CHIME_BYTES / 24u;
    params.features = 0u;
    params.channels = 2u;
    params.format = SND_PCM_FMT_S16;
    params.rate = SND_PCM_RATE_48000;
    if (!submit_control(&params, sizeof(params), &command_status, sizeof(command_status), SND_S_OK)) return 0;
    bytes_zero(&pcm_command, sizeof(pcm_command));
    pcm_command.hdr.code = SND_R_PCM_PREPARE;
    pcm_command.stream_id = 0u;
    if (!submit_control(&pcm_command, sizeof(pcm_command), &command_status, sizeof(command_status), SND_S_OK)) return 0;
    pcm_command.hdr.code = SND_R_PCM_START;
    if (!submit_control(&pcm_command, sizeof(pcm_command), &command_status, sizeof(command_status), SND_S_OK)) return 0;
    return 1;
}

static const int16_t sine_64[64] = {
    0,3212,6393,9512,12539,15446,18204,20787,23170,25330,27245,28898,30273,31356,32137,32609,
    32767,32609,32137,31356,30273,28898,27245,25330,23170,20787,18204,15446,12539,9512,6393,3212,
    0,-3212,-6393,-9512,-12539,-15446,-18204,-20787,-23170,-25330,-27245,-28898,-30273,-31356,-32137,-32609,
    -32767,-32609,-32137,-31356,-30273,-28898,-27245,-25330,-23170,-20787,-18204,-15446,-12539,-9512,-6393,-3212
};
static void synthesize_chime(void) {
    uint32_t i;
    uint64_t phase = 0u;
    uint32_t previous_frequency = 0u;
    for (i = 0u; i < SND_CHIME_FRAMES; ++i) {
        uint32_t frequency = 0u, note_position = 0u, note_length = 0u;
        int32_t sample = 0;
        if (i < 6000u) { frequency = 659u; note_position = i; note_length = 6000u; }
        else if (i >= 6500u && i < 12200u) { frequency = 784u; note_position = i - 6500u; note_length = 5700u; }
        else if (i >= 13000u && i < 19400u) { frequency = 988u; note_position = i - 13000u; note_length = 6400u; }
        else if (i >= 20200u) { frequency = 1175u; note_position = i - 20200u; note_length = SND_CHIME_FRAMES - 20200u; }
        if (frequency != 0u) {
            uint64_t step = ((uint64_t)frequency << 32) / 48000u;
            int32_t envelope = 20000;
            if (frequency != previous_frequency) phase = 0u;
            phase += step;
            sample = sine_64[(uint32_t)(phase >> 26) & 63u];
            if (note_position < 192u) envelope = (int32_t)(note_position * 20000u / 192u);
            else if (note_position + 192u > note_length) envelope = (int32_t)((note_length - note_position) * 20000u / 192u);
            sample = sample * envelope / 32767;
            previous_frequency = frequency;
        } else {
            previous_frequency = 0u;
        }
        chime_samples[(size_t)i * 2u] = (int16_t)sample;
        chime_samples[(size_t)i * 2u + 1u] = (int16_t)sample;
    }
}

int chos_audio_init(void) {
    uintptr_t first_base = platform_virtio_mmio_base();
    uint32_t slot, features1, status;
    size_t i;
    sound_ready = 0; sound_pending = 0; sound_chime_started = 0; sound_chime_state = CHOS_AUDIO_CHIME_IDLE; snd_base = 0u;
    sound_error = "no VirtIO-Sound device found";
    if (first_base == 0u) return 0;
    bytes_zero(queues, sizeof(queues));
    bytes_zero(event_buffers, sizeof(event_buffers));
    for (slot = 0u; slot < VIRTIO_MMIO_SLOT_COUNT; ++slot) {
        uintptr_t candidate = first_base + (uintptr_t)slot * VIRTIO_MMIO_SLOT_SIZE;
        if (*(volatile uint32_t *)(candidate) == VIRTIO_MAGIC &&
            *(volatile uint32_t *)(candidate + 0x008u) == VIRTIO_DEVICE_SOUND) {
            snd_base = candidate; break;
        }
    }
    if (snd_base == 0u) return 0;
    if (read_reg(REG_VERSION) < 2u) { sound_error = "legacy VirtIO-Sound transport is unsupported"; return 0; }
    playback_stream_count = read_config32(4u);
    set_status(0u);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    write_reg(REG_DEVICE_FEATURES_SEL, 1u); features1 = read_reg(REG_DEVICE_FEATURES);
    if ((features1 & VIRTIO_F_VERSION_1) == 0u) {
        set_status(VIRTIO_STATUS_FAILED); sound_error = "VirtIO-Sound does not offer VERSION_1"; return 0;
    }
    write_reg(REG_DRIVER_FEATURES_SEL, 0u); write_reg(REG_DRIVER_FEATURES, 0u);
    write_reg(REG_DRIVER_FEATURES_SEL, 1u); write_reg(REG_DRIVER_FEATURES, VIRTIO_F_VERSION_1);
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    set_status(status);
    if ((read_reg(REG_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        set_status(VIRTIO_STATUS_FAILED); sound_error = "VirtIO-Sound rejected negotiated features"; return 0;
    }
    for (i = 0u; i < SND_QUEUE_COUNT; ++i) {
        if (!configure_queue((uint16_t)i)) {
            set_status(VIRTIO_STATUS_FAILED); sound_error = "VirtIO-Sound queue setup failed"; return 0;
        }
    }
    set_status(status | VIRTIO_STATUS_DRIVER_OK);
    if (!configure_playback()) { sound_ready = 0; return 0; }
    sound_ready = 1;
    synthesize_chime();
    sound_error = "ok";
    return 1;
}

int chos_audio_available(void) { return sound_ready; }
const char *chos_audio_last_error(void) { return sound_error; }
chos_audio_chime_state_t chos_audio_startup_chime_state(void) { return sound_chime_state; }

int chos_audio_play_startup_chime(void) {
    snd_queue_t *q = &queues[2];
    uint16_t slot;
    if (!sound_ready) return 0;
    if (sound_pending || sound_chime_started) return sound_pending ? 1 : 0;
    bytes_zero(&xfer_header, sizeof(xfer_header));
    bytes_zero(&xfer_status, sizeof(xfer_status));
    xfer_header.stream_id = 0u;
    q->desc[0].addr = (uint64_t)(uintptr_t)&xfer_header;
    q->desc[0].len = sizeof(xfer_header);
    q->desc[0].flags = VIRTQ_DESC_F_NEXT;
    q->desc[0].next = 1u;
    q->desc[1].addr = (uint64_t)(uintptr_t)chime_samples;
    q->desc[1].len = (uint32_t)SND_CHIME_BYTES;
    q->desc[1].flags = VIRTQ_DESC_F_NEXT;
    q->desc[1].next = 2u;
    q->desc[2].addr = (uint64_t)(uintptr_t)&xfer_status;
    q->desc[2].len = sizeof(xfer_status);
    q->desc[2].flags = VIRTQ_DESC_F_WRITE;
    q->desc[2].next = 0u;
    clean_range_to_poc(q->desc, sizeof(q->desc));
    clean_range_to_poc(&xfer_header, sizeof(xfer_header));
    clean_range_to_poc(chime_samples, SND_CHIME_BYTES);
    clean_range_to_poc(&xfer_status, sizeof(xfer_status));
    slot = queue_avail_index[2] & (uint16_t)(q->size - 1u);
    q->avail.ring[slot] = 0u;
    dma_store_barrier();
    ++queue_avail_index[2];
    q->avail.idx = queue_avail_index[2];
    clean_range_to_poc((const void *)&q->avail, sizeof(q->avail));
    write_reg(REG_QUEUE_NOTIFY, 2u);
    sound_pending = 1;
    sound_chime_started = 1;
    sound_chime_state = CHOS_AUDIO_CHIME_PENDING;
    sound_error = "startup chime queued";
    return 1;
}

void chos_audio_pump(void) {
    snd_queue_t *q = &queues[2];
    if (!sound_ready || !sound_pending) return;
    invalidate_range_from_poc((const void *)&q->used, sizeof(q->used));
    dma_load_barrier();
    if (q->used.idx == queue_last_used[2]) return;
    invalidate_range_from_poc(&xfer_status, sizeof(xfer_status));
    ++queue_last_used[2];
    sound_pending = 0;
    if (xfer_status.status != SND_S_OK) {
        sound_error = "startup chime transfer failed";
        sound_chime_started = 0;
        sound_chime_state = CHOS_AUDIO_CHIME_FAILED;
    } else {
        sound_error = "startup chime played";
        sound_chime_state = CHOS_AUDIO_CHIME_PLAYED;
    }
}
