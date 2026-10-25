#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/sdio.h"
#include "chos/cyw_sdio.h"

typedef struct {
    chos_sdio_request_t requests[1024];
    unsigned calls, fail_at, fail_code, power_waits;
    uint64_t now, advance;
    uint32_t r5, ocr, r6, r1;
    uint8_t regs[3][0x20000];
    uint8_t memory[0x20000];
    uint8_t fifo[0x20000];
    size_t fifo_pos;
} mock_t;
static mock_t mock;
static uint64_t now_ms(void *p) { return ((mock_t *)p)->now; }
static int request(void *context, const chos_sdio_request_t *r, uint32_t *response,
                   uint32_t timeout) {
    mock_t *m = context;
    assert(timeout == CHOS_SDIO_TIMEOUT_MS);
    assert(m->calls < sizeof(m->requests) / sizeof(m->requests[0]));
    m->requests[m->calls++] = *r;
    m->now += m->advance;
    if (m->fail_at != 0 && m->calls == m->fail_at) return -(int)m->fail_code;
    *response = 0;
    switch (r->index) {
    case 5:
        assert(r->response == CHOS_SDIO_R4 && r->bytes == 0);
        *response = m->ocr;
        if (r->argument != 0) {
            assert(r->argument == 0x300000);
            if (m->power_waits != 0) --m->power_waits;
            else *response |= 0x80000000;
        }
        return CHOS_SDIO_OK;
    case 3:
        assert(r->response == CHOS_SDIO_R6 && r->argument == 0);
        *response = m->r6;
        return CHOS_SDIO_OK;
    case 7:
        assert(r->response == CHOS_SDIO_R1B && r->argument == 0x12340000);
        *response = m->r1;
        return CHOS_SDIO_OK;
    case 52: {
        unsigned fn = (r->argument >> 28) & 7u;
        uint32_t addr = (r->argument >> 9) & 0x1ffff;
        int write = (r->argument >> 31) != 0;
        assert(fn < 3 && (r->argument & 0x04000100) == 0);
        assert(r->response == CHOS_SDIO_R5 && r->bytes == 0);
        if (write) m->regs[fn][addr] = (uint8_t)r->argument;
        *response = m->regs[fn][addr] | m->r5;
        return CHOS_SDIO_OK;
    }
    case 53: {
        unsigned fn = (r->argument >> 28) & 7u;
        uint32_t addr = (r->argument >> 9) & 0x1ffff;
        unsigned count = r->argument & 511u;
        int block = (r->argument >> 27) & 1u;
        int increment = (r->argument >> 26) & 1u;
        int write = (r->argument >> 31) != 0;
        assert(fn == 1 || fn == 2);
        assert(r->response == CHOS_SDIO_R5 && r->write == write);
        assert(r->bytes == (size_t)r->block_size * r->block_count);
        assert(block ? count == r->block_count && count != 0 :
            r->block_count == 1 && (count ? count : 512u) == r->bytes);
        uint8_t *buffer = r->data;
        for (size_t i = 0; i < r->bytes; ++i) {
            uint8_t *target;
            if (fn == 2) {
                assert(!increment && m->fifo_pos < sizeof(m->fifo));
                target = &m->fifo[m->fifo_pos++];
            } else if (addr & 0x8000) {
                uint32_t window = ((uint32_t)m->regs[1][0x1000a] << 8) |
                    ((uint32_t)m->regs[1][0x1000b] << 16) |
                    ((uint32_t)m->regs[1][0x1000c] << 24);
                uint32_t effective = window + (addr & 0x7fff) + (increment ? (uint32_t)i : 0);
                assert(effective >= 0x18000000 && effective < 0x18020000);
                target = &m->memory[effective - 0x18000000];
            } else {
                uint32_t effective = addr + (increment ? (uint32_t)i : 0);
                assert(effective <= CHOS_SDIO_ADDRESS_MAX);
                target = &m->regs[fn][effective];
            }
            if (write) *target = buffer[i];
            else buffer[i] = *target;
        }
        *response = m->r5;
        return CHOS_SDIO_OK;
    }
    default: assert(0); return CHOS_SDIO_IO;
    }
}
static const chos_sdio_host_ops_t ops = {
    .request = request, .now_ms = now_ms, .maximum_transfer = 4096, .pio_only = 1
};
static void start(chos_sdio_bus_t *bus) {
    /* This fixture represents a fresh host/card power cycle, not retry of a
     * quarantined physical controller. Production code must reset real power. */
    *bus = (chos_sdio_bus_t){0};
    memset(&mock, 0, sizeof(mock));
    mock.ocr = 0x20300000;
    mock.r6 = 0x12340000;
    mock.regs[0][0] = 0x43;
    mock.regs[0][8] = 2; /* actual multi-block capability, not assumed */
    mock.regs[0][9] = 0; mock.regs[0][10] = 0x10;
    static const uint8_t cis[] = {0, 0x20, 4, 0xd0, 2, 0xbf, 0xa9, 0xff};
    memcpy(mock.regs[0] + 0x1000, cis, sizeof(cis));
    assert(chos_sdio_init(bus, &ops, &mock, 0x300000) == CHOS_SDIO_OK);
}
static void ready(chos_sdio_bus_t *bus) {
    start(bus);
    for (unsigned i = 0; i < 5; ++i) assert(chos_sdio_probe_step(bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(bus) == CHOS_SDIO_OK);
    assert(bus->state == CHOS_SDIO_READY && bus->rca == 0x1234 && bus->functions == 2);
    assert(bus->cccr_revision == 3 && bus->sdio_revision == 4);
    uint16_t vendor, device;
    assert(chos_sdio_read_identity(bus, &vendor, &device) == 0 && vendor == 0x02d0 && device == 0xa9bf);
}
static void enable(chos_sdio_bus_t *bus, unsigned fn, unsigned block) {
    assert(chos_sdio_enable_function(bus, fn) == CHOS_SDIO_OK);
    assert(chos_sdio_poll_function(bus, fn) == CHOS_SDIO_PENDING);
    mock.regs[0][3] |= (uint8_t)(1u << fn);
    assert(chos_sdio_poll_function(bus, fn) == CHOS_SDIO_OK);
    assert(chos_sdio_set_block_size(bus, fn, block) == CHOS_SDIO_OK);
    assert(bus->block_size[fn] == block);
}
static void arguments(void) {
    uint32_t out = 0x12345678;
    assert(chos_sdio_cmd52_argument(1, 1, 0x1000a, 1, 0x80, &out) == 0);
    assert(out == (0x98000000 | (0x1000a << 9) | 0x80));
    assert(chos_sdio_cmd52_argument(0, 7, 0x1ffff, 0, 0, &out) == 0);
    assert(out == 0x73fffe00);
    uint32_t saved = out;
    assert(chos_sdio_cmd52_argument(0, 0, 0, 1, 0, &out) == CHOS_SDIO_INVALID);
    assert(chos_sdio_cmd52_argument(1, 8, 0, 0, 0, &out) == CHOS_SDIO_INVALID);
    assert(chos_sdio_cmd52_argument(1, 1, 0x20000, 0, 0, &out) == CHOS_SDIO_INVALID);
    assert(out == saved);
    assert(chos_sdio_cmd53_argument(1, 2, 0, 0, 0, 512, &out) == 0 && out == 0xa0000000);
    assert(chos_sdio_cmd53_argument(0, 1, 0x8000, 1, 1, 8, &out) == 0 && out == 0x1d000008);
    saved = out;
    assert(chos_sdio_cmd53_argument(0, 1, 0, 0, 1, 512, &out) == CHOS_SDIO_INVALID);
    assert(chos_sdio_cmd53_argument(0, 1, 0, 0, 0, 0, &out) == CHOS_SDIO_INVALID);
    assert(chos_sdio_cmd53_argument(0, 1, 0, 2, 0, 1, &out) == CHOS_SDIO_INVALID);
    assert(out == saved);
}
static void discovery(void) {
    chos_sdio_bus_t bus;
    ready(&bus);
    unsigned calls = mock.calls;
    assert(chos_sdio_probe_step(&bus) == 0 && mock.calls == calls);
    assert(chos_sdio_init(&bus, &ops, &mock, 0x300000) == CHOS_SDIO_INVALID);
    uint8_t value;
    assert(chos_sdio_readb(&bus, 1, 0, &value) == CHOS_SDIO_INVALID && mock.calls == calls);
    assert(chos_sdio_enable_function(&bus, 0) == CHOS_SDIO_INVALID);
    assert(chos_sdio_enable_function(&bus, 3) == CHOS_SDIO_INVALID);
    enable(&bus, 1, 64);
    enable(&bus, 2, 512);
    assert(mock.regs[0][0x110] == 64 && mock.regs[0][0x111] == 0);
    assert(mock.regs[0][0x210] == 0 && mock.regs[0][0x211] == 2);
    start(&bus);
    mock.now = CHOS_SDIO_TIMEOUT_MS;
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_TIMEOUT && mock.calls == 0);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_QUARANTINED);
    start(&bus);
    mock.power_waits = 40;
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    for (unsigned i = 0; i < 31; ++i) assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_TIMEOUT && mock.calls == 33);
    const uint32_t bad_ocr[] = {0x300000, 0x28300000, 0x20008000};
    for (unsigned i = 0; i < sizeof(bad_ocr) / sizeof(bad_ocr[0]); ++i) {
        start(&bus); mock.ocr = bad_ocr[i];
        assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_UNSUPPORTED);
    }
    chos_sdio_host_ops_t bad = ops;
    bad.pio_only = 0;
    assert(chos_sdio_init(&bus, &bad, &mock, 0x300000) == CHOS_SDIO_INVALID);
    bad = ops; bad.maximum_transfer = 8192;
    assert(chos_sdio_init(&bus, &bad, &mock, 0x300000) == CHOS_SDIO_INVALID);
    assert(chos_sdio_init(&bus, &ops, &mock, 0x80000000) == CHOS_SDIO_INVALID);
    start(&bus); mock.r6 = 0x00000000;
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_CARD_ERROR);
    start(&bus); mock.r6 = 0x12348000;
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_CARD_ERROR);
    start(&bus); mock.r1 = 0x80000000;
    for (unsigned i = 0; i < 3; ++i) assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_CARD_ERROR);
    start(&bus); mock.regs[0][0] = 0xf0;
    for (unsigned i = 0; i < 4; ++i) assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_PENDING);
    assert(chos_sdio_probe_step(&bus) == CHOS_SDIO_UNSUPPORTED);
}
static void errors(void) {
    chos_sdio_bus_t bus;
    const uint32_t errors[] = {0x8000,0x4000,0x0800,0x0200,0x0100};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        ready(&bus); mock.r5 = errors[i];
        uint8_t out = 0x5a;
        assert(chos_sdio_readb(&bus, 0, 1, &out) == CHOS_SDIO_CARD_ERROR && out == 0x5a);
        unsigned calls = mock.calls;
        assert(chos_sdio_readb(&bus, 0, 1, &out) == CHOS_SDIO_QUARANTINED);
        assert(mock.calls == calls && bus.ready == 0);
    }
    ready(&bus); mock.fail_at = mock.calls + 1; mock.fail_code = -CHOS_SDIO_TIMEOUT;
    uint8_t out = 0;
    assert(chos_sdio_readb(&bus, 0, 0, &out) == CHOS_SDIO_TIMEOUT);
    ready(&bus); mock.fail_at = mock.calls + 1; mock.fail_code = 99;
    assert(chos_sdio_writeb(&bus, 0, 0, 0) == CHOS_SDIO_IO);
}
static void identity(void) {
    chos_sdio_bus_t bus;
    uint16_t vendor = 0x1234, device = 0x5678;
    ready(&bus);
    unsigned before = mock.calls;
    assert(chos_sdio_read_identity(&bus, &vendor, &device) == 0 && mock.calls == before);
    assert(vendor == 0x02d0 && device == 0xa9bf);
    const uint8_t malformed[][9] = {
        {0x20,3,0xd0,2,0xbf,0xff,0xff,0xff,0xff},
        {0x20,4,0xd0,2,0xbf,0xa9,0x20,4,0xff},
        {0xff,0,0,0,0,0,0,0,0}
    };
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        ready(&bus); bus.identity_valid = 0;
        memcpy(mock.regs[0] + 0x1000, malformed[i], sizeof(malformed[i]));
        vendor = 0x1234; device = 0x5678;
        assert(chos_sdio_read_identity(&bus, &vendor, &device) == CHOS_SDIO_CARD_ERROR);
        assert(vendor == 0x1234 && device == 0x5678 && bus.state == CHOS_SDIO_FAILED);
    }
    ready(&bus); bus.identity_valid = 0;
    memset(mock.regs[0] + 0x1000, 0, 512);
    assert(chos_sdio_read_identity(&bus, &vendor, &device) == CHOS_SDIO_CARD_ERROR);
    ready(&bus); bus.identity_valid = 0;
    mock.regs[0][9] = mock.regs[0][10] = mock.regs[0][11] = 0xff;
    before = mock.calls;
    assert(chos_sdio_read_identity(&bus, &vendor, &device) == CHOS_SDIO_CARD_ERROR);
    assert(mock.calls == before + 3);
}
static void transfers(void) {
    chos_sdio_bus_t bus;
    uint8_t data[CHOS_SDIO_TRANSFER_MAX];
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 3u);
    ready(&bus); enable(&bus, 1, 64);
    unsigned before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 1, 0x1000, 1, data, 1027) == 0);
    assert(mock.calls == before + 2);
    assert(mock.requests[before].bytes == 1024 && mock.requests[before].block_count == 16);
    assert(mock.requests[before + 1].bytes == 3 && !(mock.requests[before + 1].argument & (1u << 27)));
    assert(memcmp(mock.regs[1] + 0x1000, data, 1027) == 0);
    assert(chos_sdio_transfer(&bus, 0, 1, 0x1000, 1, data, 1027) == 0);
    before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 1, 0x1ffff, 1, data, 2) == CHOS_SDIO_INVALID);
    assert(chos_sdio_transfer(&bus, 1, 1, 0, 1, data, 0) == CHOS_SDIO_INVALID);
    assert(chos_sdio_transfer(&bus, 1, 1, 0, 1, data, sizeof(data) + 1) == CHOS_SDIO_INVALID);
    assert(mock.calls == before);
    enable(&bus, 2, 512);
    before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 2, 0, 0, data, 1024) == 0);
    assert(mock.calls == before + 1 && mock.requests[before].block_count == 2);
    assert(memcmp(mock.fifo, data, 1024) == 0);
    /* Byte mode encodes 512 as zero; it is never interpreted as an empty I/O. */
    bus.block_size[2] = 0;
    before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 2, 0, 0, data, 512) == 0);
    assert((mock.requests[before].argument & 511u) == 0 && mock.requests[before].bytes == 512);
    before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 2, 0, 0, data, sizeof(data)) == 0);
    assert(mock.calls == before + 64);
    bus.cccr_capabilities = 0;
    bus.block_size[2] = 512;
    before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 2, 0, 0, data, 1024) == 0);
    assert(mock.calls == before + 2 && !(mock.requests[before].argument & (1u << 27)));
    bus.block_size[2] = 1;
    mock.fifo_pos = 0;
    before = mock.calls;
    assert(chos_sdio_transfer(&bus, 1, 2, 0, 0, data, sizeof(data)) == 0);
    assert(mock.calls == before + 64);
    mock.advance = 1001;
    assert(chos_sdio_transfer(&bus, 1, 2, 0, 0, data, 1024) == CHOS_SDIO_TIMEOUT);
    assert(bus.state == CHOS_SDIO_FAILED);
}
static void backplane(void) {
    chos_sdio_bus_t bus;
    chos_cyw_sdio_t radio;
    ready(&bus);
    assert(chos_cyw_sdio_init(&radio, &bus) == CHOS_SDIO_INVALID);
    enable(&bus, 1, 64);
    assert(chos_cyw_sdio_init(&radio, &bus) == 0);
    bus.vendor = 0x1234;
    assert(chos_cyw_sdio_init(&radio, &bus) == CHOS_SDIO_UNSUPPORTED);
    bus.vendor = 0x02d0;
    assert(chos_cyw_sdio_init(&radio, &bus) == 0);
    uint8_t data[] = {1,2,3,4,5,6,7,8};
    unsigned before = mock.calls;
    assert(chos_cyw_backplane_transfer(&radio, 1, 0x18007ffc, data, sizeof(data)) == 0);
    assert(mock.calls == before + 8); /* three window registers + data, twice */
    assert(memcmp(mock.memory + 0x7ffc, data, sizeof(data)) == 0);
    uint32_t value = 0;
    before = mock.calls;
    assert(chos_cyw_backplane_read32(&radio, 0x18008000, &value) == 0 && value == 0x08070605);
    assert(mock.calls == before + 1); /* cached window, actual read */
    assert(chos_cyw_backplane_write32(&radio, 0x18008004, 0x12345678) == 0);
    assert(mock.memory[0x8004] == 0x78 && mock.memory[0x8007] == 0x12);
    before = mock.calls;
    assert(chos_cyw_backplane_read32(&radio, 0x18008001, &value) == CHOS_SDIO_INVALID);
    assert(chos_cyw_backplane_transfer(&radio, 1, 0xfffffffc, data, 8) == CHOS_SDIO_INVALID);
    assert(mock.calls == before);
    mock.memory[0] = 0x45; mock.memory[1] = 0x43; mock.memory[2] = 6; mock.memory[3] = 0x10;
    chos_cyw_chip_id_t chip;
    assert(chos_cyw_read_chip_id(&radio, &chip) == 0 && chip.chip == 0x4345 && chip.revision == 6 && chip.type == 1);
    mock.memory[2] = 9;
    assert(chos_cyw_read_chip_id(&radio, &chip) == CHOS_SDIO_UNSUPPORTED && chip.revision == 9);
    mock.fail_at = mock.calls + 2; mock.fail_code = -CHOS_SDIO_TIMEOUT;
    assert(chos_cyw_backplane_read32(&radio, 0x18010000, &value) == CHOS_SDIO_TIMEOUT);
    assert(!radio.window_valid && bus.state == CHOS_SDIO_FAILED);
    before = mock.calls;
    assert(chos_cyw_backplane_write32(&radio, 0x18000000, 0) == CHOS_SDIO_QUARANTINED);
    assert(mock.calls == before);
}
static void sdpcm(void) {
    uint8_t frame[CHOS_CYW_SDPCM_MAX + 4];
    const uint8_t payload[] = {0x11,0x22,0x33,0x44,0x55};
    size_t used = 0;
    chos_cyw_sdpcm_frame_t parsed = {0};
    for (unsigned channel = 0; channel <= 2; ++channel) {
        memset(frame, 0xa5, sizeof(frame));
        assert(chos_cyw_sdpcm_encode(frame, sizeof(frame), channel, 255, payload, sizeof(payload), &used) == 0);
        assert(used == 20 && frame[0] == 17 && frame[1] == 0 && frame[2] == 0xee && frame[3] == 0xff);
        assert(frame[17] == 0 && frame[18] == 0 && frame[19] == 0 && frame[20] == 0xa5);
        frame[6] = 4; frame[8] = 0x7; frame[9] = 3;
        assert(chos_cyw_sdpcm_decode(frame, used, &parsed) == 0);
        assert(parsed.payload_bytes == sizeof(payload) && memcmp(parsed.payload, payload, sizeof(payload)) == 0);
        assert(parsed.channel == channel && parsed.sequence == 255 && parsed.flow_control == 7 &&
            parsed.transmit_limit == 3 && parsed.next_length_16 == 4);
    }
    chos_cyw_sdpcm_frame_t saved = parsed;
    frame[2] ^= 1;
    assert(chos_cyw_sdpcm_decode(frame, used, &parsed) == CHOS_SDIO_INVALID);
    assert(memcmp(&parsed, &saved, sizeof(saved)) == 0);
    frame[2] ^= 1; frame[7] = 11;
    assert(chos_cyw_sdpcm_decode(frame, used, &parsed) == CHOS_SDIO_INVALID);
    frame[7] = 18;
    assert(chos_cyw_sdpcm_decode(frame, used, &parsed) == CHOS_SDIO_INVALID);
    frame[7] = 12; frame[5] = 0x83;
    assert(chos_cyw_sdpcm_decode(frame, used, &parsed) == CHOS_SDIO_UNSUPPORTED);
    frame[5] = 2;
    assert(chos_cyw_sdpcm_decode(frame, 16, &parsed) == CHOS_SDIO_INVALID);
    memset(frame, 0, 12);
    assert(chos_cyw_sdpcm_decode(frame, 12, &parsed) == CHOS_SDIO_EMPTY);
    assert(chos_cyw_sdpcm_encode(frame, 12, 0, 0, NULL, 0, &used) == 0 && used == 12);
    assert(chos_cyw_sdpcm_decode(frame, used, &parsed) == 0 && parsed.payload_bytes == 0);
    assert(chos_cyw_sdpcm_encode(frame, 12, 0, 0, payload, 1, &used) == CHOS_SDIO_CAPACITY);
    assert(chos_cyw_sdpcm_encode(frame, sizeof(frame), 0, 0, frame + 12, 1, &used) == CHOS_SDIO_INVALID);
    /* Deterministic mutation/truncation through the real bounded parser. */
    assert(chos_cyw_sdpcm_encode(frame, sizeof(frame), 2, 0, payload, sizeof(payload), &used) == 0);
    for (unsigned i = 0; i < 12; ++i) for (unsigned bit = 0; bit < 8; ++bit) {
        frame[i] ^= (uint8_t)(1u << bit);
        int rc = chos_cyw_sdpcm_decode(frame, used, &parsed);
        assert(rc == 0 || rc == CHOS_SDIO_INVALID || rc == CHOS_SDIO_UNSUPPORTED);
        if (rc == 0) assert(parsed.payload >= frame + 12 && parsed.payload + parsed.payload_bytes <= frame + used);
        frame[i] ^= (uint8_t)(1u << bit);
    }
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void nvram(void) {
    static const uint8_t mac[6] = {0xdc,0xa6,0x32,0x01,0x02,0x03};
    const char input[] = "# calibration\r\n boardrev = 0x1304 # keep exact\r\nccode=US\nregrev=0\n"
        "macaddr=b8:27:eb:74:f2:6c\nbtc_mode=1\nbtc_mode=1\n";
    static const char expected[] = "boardrev=0x1304\0ccode=US\0regrev=0\0"
        "macaddr=dc:a6:32:01:02:03\0btc_mode=1\0";
    uint8_t output[CHOS_CYW_NVRAM_MAX + 8];
    memset(output, 0x5a, sizeof(output));
    size_t used = 999;
    assert(chos_cyw_nvram_pack(input, sizeof(input), mac, output, sizeof(output), &used) == 0);
    assert(memcmp(output, expected, sizeof(expected) - 1) == 0);
    assert(used % 4 == 0 && output[used] == 0x5a && output[used - 5] == 0);
    uint32_t token = le32(output + used - 4);
    assert((token & 0xffffu) == (used - 4) / 4 && (uint16_t)(token >> 16) == (uint16_t)~token);
    const char *bad[] = {"a=1\na=2\n", "devpath0=pcie/1/4/\n0:x=y\n", "a\n", "=b\n", "# empty\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(output, 0x5a, sizeof(output)); used = 999;
        assert(chos_cyw_nvram_pack(bad[i], strlen(bad[i]), mac, output, sizeof(output), &used) < 0);
        assert(used == 999);
        for (unsigned j = 0; j < sizeof(output); ++j) assert(output[j] == 0x5a);
    }
    const uint8_t hidden[] = {'a','=','1',0,'b','=','2'};
    assert(chos_cyw_nvram_pack(hidden, sizeof(hidden), mac, output, sizeof(output), &used) == CHOS_SDIO_INVALID);
    uint8_t invalid_mac[6] = {0};
    assert(chos_cyw_nvram_pack(input, sizeof(input), invalid_mac, output, sizeof(output), &used) == CHOS_SDIO_INVALID);
    invalid_mac[0] = 1;
    assert(chos_cyw_nvram_pack(input, sizeof(input), invalid_mac, output, sizeof(output), &used) == CHOS_SDIO_INVALID);
    assert(chos_cyw_nvram_pack(input, sizeof(input), mac, output, 4, &used) == CHOS_SDIO_CAPACITY);
    assert(chos_cyw_nvram_pack(output, 16, mac, output, sizeof(output), &used) == CHOS_SDIO_INVALID);
    const char simple[] = "a=1";
    assert(chos_cyw_nvram_pack(simple, sizeof(simple) - 1, mac, output, sizeof(output), &used) == 0);
    assert(memcmp(output, "a=1\0macaddr=dc:a6:32:01:02:03\0", 30) == 0);
    char many[4096];
    size_t pos = 0;
    for (unsigned i = 0; i <= CHOS_CYW_NVRAM_ENTRIES; ++i)
        pos += (size_t)snprintf(many + pos, sizeof(many) - pos, "key%u=1\n", i);
    assert(chos_cyw_nvram_pack(many, pos, mac, output, sizeof(output), &used) == CHOS_SDIO_CAPACITY);
    char boundary[1100];
    memset(boundary, 'k', 63); memcpy(boundary + 63, "=v", 2);
    assert(chos_cyw_nvram_pack(boundary, 65, mac, output, sizeof(output), &used) == 0);
    boundary[63] = 'k'; memcpy(boundary + 64, "=v", 2);
    assert(chos_cyw_nvram_pack(boundary, 66, mac, output, sizeof(output), &used) == CHOS_SDIO_INVALID);
    boundary[0] = 'k'; boundary[1] = '='; memset(boundary + 2, 'v', 1023);
    assert(chos_cyw_nvram_pack(boundary, 1025, mac, output, sizeof(output), &used) == 0);
    boundary[1025] = 'v';
    assert(chos_cyw_nvram_pack(boundary, 1026, mac, output, sizeof(output), &used) == CHOS_SDIO_INVALID);
    uint32_t random = 0x43455;
    uint8_t fuzz[128], short_output[256];
    for (unsigned iteration = 0; iteration < 256; ++iteration) {
        for (unsigned i = 0; i < sizeof(fuzz); ++i) {
            random = random * 1664525u + 1013904223u;
            fuzz[i] = (uint8_t)(random >> 24);
        }
        memset(short_output, 0x5a, sizeof(short_output));
        used = 999;
        int rc = chos_cyw_nvram_pack(fuzz, 1u + iteration % sizeof(fuzz), mac,
                                     short_output, sizeof(short_output), &used);
        assert(rc == 0 || rc == CHOS_SDIO_INVALID || rc == CHOS_SDIO_CAPACITY ||
               rc == CHOS_SDIO_UNSUPPORTED);
        if (rc != 0) {
            assert(used == 999);
            for (unsigned i = 0; i < sizeof(short_output); ++i) assert(short_output[i] == 0x5a);
        } else assert(used <= sizeof(short_output));
    }
}
static void board_nvram(const char *path) {
    uint8_t input[CHOS_CYW_NVRAM_MAX + 1], output[CHOS_CYW_NVRAM_MAX];
    /* Fixture MAC only. This never reaches a radio and is not a production
     * fallback: real firmware preparation requires the platform's actual MAC. */
    static const uint8_t mac[6] = {0x02,0,0,0,0,1};
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    size_t bytes = fread(input, 1, sizeof(input), file);
    assert(!ferror(file) && feof(file) && fclose(file) == 0);
    size_t used = 0;
    assert(chos_cyw_nvram_pack(input, bytes, mac, output, sizeof(output), &used) == 0);
    assert(used <= sizeof(output));
    printf("Pinned board NVRAM fixture: %zu source bytes -> %zu packed bytes (host model only)\n", bytes, used);
}
int main(int argc, char **argv) {
    arguments(); discovery(); errors(); identity(); transfers(); backplane(); sdpcm(); nvram();
    assert(argc == 1 || argc == 2);
    if (argc == 2) board_nvram(argv[1]);
    puts("SDIO/CYW protocol: native enumeration, bounded CMD52/53, function readiness, block/FIFO I/O, quarantine, 32KiB backplane windows, chip ID, SDPCM and board-MAC NVRAM passed (host model only)");
    return 0;
}
