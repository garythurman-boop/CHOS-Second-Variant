#include "chos/cyw_sdio.h"

#define WINDOW_MASK UINT32_C(0xffff8000)
#define WINDOW_LOW UINT32_C(0x1000a)
#define ACCESS_WORD UINT32_C(0x8000)
#define CHIPCOMMON_BASE UINT32_C(0x18000000)

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn) {
    if (an == 0 || bn == 0) return 0;
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y) return 1;
    return x < y + bn && y < x + an;
}
static int valid(const chos_cyw_sdio_t *r) {
    return r != NULL && r->bus != NULL && r->bus->state == CHOS_SDIO_READY &&
        (r->bus->ready & 2u) && r->bus->block_size[1] == 64;
}
int chos_cyw_sdio_init(chos_cyw_sdio_t *radio, chos_sdio_bus_t *bus) {
    if (radio == NULL || bus == NULL || bus->state != CHOS_SDIO_READY ||
        bus->functions < 2 || !(bus->ready & 2u) || bus->block_size[1] != 64 ||
        !bus->identity_valid)
        return CHOS_SDIO_INVALID;
    if (bus->vendor != 0x02d0 || (bus->device != 0x4345 && bus->device != 0xa9bf))
        return CHOS_SDIO_UNSUPPORTED;
    *radio = (chos_cyw_sdio_t){.bus = bus};
    return CHOS_SDIO_OK;
}
static int timeout(chos_cyw_sdio_t *radio, uint64_t started) {
    chos_sdio_bus_t *bus = radio->bus;
    if (bus->host->now_ms(bus->context) - started < CHOS_SDIO_TIMEOUT_MS) return 0;
    radio->window_valid = 0;
    bus->state = CHOS_SDIO_FAILED;
    bus->ready = 0;
    bus->error = CHOS_SDIO_TIMEOUT;
    return 1;
}
static int window(chos_cyw_sdio_t *radio, uint32_t address, uint64_t started) {
    uint32_t base = address & WINDOW_MASK;
    if (radio->window_valid && radio->window == base) return CHOS_SDIO_OK;
    radio->window_valid = 0;
    uint32_t v = base >> 8;
    for (unsigned i = 0; i < 3; ++i) {
        if (timeout(radio, started)) return CHOS_SDIO_TIMEOUT;
        int rc = chos_sdio_writeb(radio->bus, 1, WINDOW_LOW + i, (uint8_t)v);
        if (rc != CHOS_SDIO_OK) return rc;
        if (timeout(radio, started)) return CHOS_SDIO_TIMEOUT;
        v >>= 8;
    }
    radio->window = base;
    radio->window_valid = 1;
    return CHOS_SDIO_OK;
}
int chos_cyw_backplane_transfer(chos_cyw_sdio_t *radio, int write, uint32_t address,
                               void *buffer, size_t bytes) {
    if (radio != NULL && radio->bus != NULL && radio->bus->state == CHOS_SDIO_FAILED)
        return CHOS_SDIO_QUARANTINED;
    if (!valid(radio) || (write != 0 && write != 1) || buffer == NULL ||
        bytes == 0 || bytes > CHOS_SDIO_TRANSFER_MAX || (bytes & 3u) ||
        (address & 3u) || bytes - 1 > UINT32_MAX - address) return CHOS_SDIO_INVALID;
    uint8_t *data = buffer;
    uint64_t started = radio->bus->host->now_ms(radio->bus->context);
    while (bytes) {
        if (timeout(radio, started)) return CHOS_SDIO_TIMEOUT;
        size_t offset = address & (CHOS_CYW_BACKPLANE_WINDOW - 1u);
        size_t chunk = CHOS_CYW_BACKPLANE_WINDOW - offset;
        if (chunk > bytes) chunk = bytes;
        int rc = window(radio, address, started);
        if (rc != CHOS_SDIO_OK) return rc;
        rc = chos_sdio_transfer(radio->bus, write, 1, (uint32_t)offset | ACCESS_WORD,
                                1, data, chunk);
        if (rc != CHOS_SDIO_OK) {
            radio->window_valid = 0;
            return rc;
        }
        if (timeout(radio, started)) return CHOS_SDIO_TIMEOUT;
        data += chunk;
        address += (uint32_t)chunk;
        bytes -= chunk;
    }
    return CHOS_SDIO_OK;
}
int chos_cyw_backplane_read32(chos_cyw_sdio_t *radio, uint32_t address, uint32_t *out) {
    uint8_t bytes[4];
    if (out == NULL) return CHOS_SDIO_INVALID;
    int rc = chos_cyw_backplane_transfer(radio, 0, address, bytes, sizeof(bytes));
    if (rc == CHOS_SDIO_OK) *out = get32(bytes);
    return rc;
}
int chos_cyw_backplane_write32(chos_cyw_sdio_t *radio, uint32_t address, uint32_t value) {
    uint8_t bytes[4];
    put32(bytes, value);
    return chos_cyw_backplane_transfer(radio, 1, address, bytes, sizeof(bytes));
}
int chos_cyw_read_chip_id(chos_cyw_sdio_t *radio, chos_cyw_chip_id_t *out) {
    if (out == NULL) return CHOS_SDIO_INVALID;
    uint32_t value;
    int rc = chos_cyw_backplane_read32(radio, CHIPCOMMON_BASE, &value);
    if (rc != CHOS_SDIO_OK) return rc;
    *out = (chos_cyw_chip_id_t){
        .chip = (uint16_t)value,
        .revision = (uint8_t)((value >> 16) & 15u),
        .package = (uint8_t)((value >> 20) & 15u),
        .type = (uint8_t)(value >> 28)
    };
    return out->chip == 0x4345 && out->revision == 6 ? CHOS_SDIO_OK : CHOS_SDIO_UNSUPPORTED;
}

int chos_cyw_sdpcm_decode(const void *buffer, size_t bytes, chos_cyw_sdpcm_frame_t *out) {
    if (buffer == NULL || out == NULL || bytes < CHOS_CYW_SDPCM_HEADER)
        return CHOS_SDIO_INVALID;
    const uint8_t *b = buffer;
    uint16_t length = get16(b), inverse = get16(b + 2);
    if (length == 0 && inverse == 0) return CHOS_SDIO_EMPTY;
    if ((uint16_t)(length ^ inverse) != UINT16_MAX || length < CHOS_CYW_SDPCM_HEADER ||
        length > CHOS_CYW_SDPCM_MAX || length > bytes ||
        b[7] < CHOS_CYW_SDPCM_HEADER || b[7] > length) return CHOS_SDIO_INVALID;
    /* Coalesced/glom packets need a separate validated descriptor parser. Never
     * mistake a glom descriptor or unsupported channel for Ethernet payload. */
    if ((b[5] & 0xf0u) != 0 || (b[5] & 15u) > 2u) return CHOS_SDIO_UNSUPPORTED;
    *out = (chos_cyw_sdpcm_frame_t){
        .payload = b + b[7], .payload_bytes = length - b[7],
        .frame_bytes = length, .sequence = b[4], .channel = b[5] & 15u,
        .next_length_16 = b[6], .flow_control = b[8], .transmit_limit = b[9]
    };
    return CHOS_SDIO_OK;
}
int chos_cyw_sdpcm_encode(void *buffer, size_t capacity, unsigned channel,
                          uint8_t sequence, const void *payload, size_t payload_bytes,
                          size_t *transfer_bytes) {
    if (buffer == NULL || transfer_bytes == NULL || channel > 2 ||
        (payload == NULL && payload_bytes != 0) ||
        payload_bytes > CHOS_CYW_SDPCM_MAX - CHOS_CYW_SDPCM_HEADER)
        return CHOS_SDIO_INVALID;
    size_t length = CHOS_CYW_SDPCM_HEADER + payload_bytes;
    size_t padded = (length + 3u) & ~(size_t)3u;
    if (padded > capacity) return CHOS_SDIO_CAPACITY;
    if (ranges_overlap(buffer, padded, payload, payload_bytes)) return CHOS_SDIO_INVALID;
    uint8_t *b = buffer;
    for (size_t i = 0; i < padded; ++i) b[i] = 0;
    put16(b, (uint16_t)length);
    put16(b + 2, (uint16_t)~length);
    b[4] = sequence;
    b[5] = (uint8_t)channel;
    b[7] = CHOS_CYW_SDPCM_HEADER;
    const uint8_t *p = payload;
    for (size_t i = 0; i < payload_bytes; ++i) b[CHOS_CYW_SDPCM_HEADER + i] = p[i];
    *transfer_bytes = padded;
    return CHOS_SDIO_OK;
}

typedef struct { uint16_t key, key_bytes, value, value_bytes; } entry_t;
static int space(uint8_t c) { return c == ' ' || c == '\t'; }
static int key_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}
static int same(const uint8_t *a, const uint8_t *b, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static int mac_key(const uint8_t *p, size_t n) {
    static const uint8_t key[] = "macaddr";
    return n == sizeof(key) - 1 && same(p, key, n);
}
int chos_cyw_nvram_pack(const void *text, size_t bytes, const uint8_t mac[6],
                        void *output, size_t capacity, size_t *packed_bytes) {
    if (text == NULL || bytes == 0 || bytes > CHOS_CYW_NVRAM_MAX || mac == NULL ||
        output == NULL || packed_bytes == NULL || (mac[0] & 1u) ||
        (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0 ||
        ranges_overlap(text, bytes, output, capacity) ||
        ranges_overlap(mac, 6, output, capacity)) return CHOS_SDIO_INVALID;
    const uint8_t *b = text;
    entry_t entries[CHOS_CYW_NVRAM_ENTRIES];
    unsigned count = 0;
    size_t pos = 0, length = 0;
    int has_mac = 0;
    while (pos < bytes) {
        size_t start = pos;
        while (pos < bytes && b[pos] != '\n' && b[pos] != '\r') {
            /* A final NUL terminator from a file reader is accepted, never
             * embedded NUL followed by hidden calibration/profile entries. */
            if (b[pos] == 0 && pos == bytes - 1) break;
            if (b[pos] == 0 || (b[pos] < 32 && b[pos] != '\t') || b[pos] > 126)
                return CHOS_SDIO_INVALID;
            ++pos;
        }
        size_t end = pos;
        if (pos < bytes && b[pos] == 0) pos = bytes;
        else {
            if (pos < bytes && b[pos] == '\r') ++pos;
            if (pos < bytes && b[pos] == '\n') ++pos;
        }
        for (size_t i = start; i < end; ++i) if (b[i] == '#') { end = i; break; }
        while (start < end && space(b[start])) ++start;
        while (end > start && space(b[end - 1])) --end;
        if (start == end) continue;
        size_t eq = start;
        while (eq < end && b[eq] != '=') ++eq;
        if (eq == end) return CHOS_SDIO_INVALID;
        size_t ke = eq;
        while (ke > start && space(b[ke - 1])) --ke;
        size_t value = eq + 1;
        while (value < end && space(b[value])) ++value;
        if (ke == start || ke - start > 63 || end - value > 1023)
            return CHOS_SDIO_INVALID;
        for (size_t i = start; i < ke; ++i) if (!key_char(b[i])) return CHOS_SDIO_UNSUPPORTED;
        int is_mac = mac_key(b + start, ke - start);
        int duplicate = 0;
        for (unsigned i = 0; i < count; ++i) {
            const entry_t *e = &entries[i];
            if (e->key_bytes == ke - start && same(b + e->key, b + start, e->key_bytes)) {
                if (!is_mac && (e->value_bytes != end - value ||
                    !same(b + e->value, b + value, e->value_bytes))) return CHOS_SDIO_INVALID;
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        if (count == CHOS_CYW_NVRAM_ENTRIES) return CHOS_SDIO_CAPACITY;
        entries[count++] = (entry_t){(uint16_t)start, (uint16_t)(ke - start),
                                     (uint16_t)value, (uint16_t)(end - value)};
        length += (ke - start) + 1 + (is_mac ? 17u : end - value) + 1;
        has_mac |= is_mac;
    }
    if (count == 0) return CHOS_SDIO_INVALID;
    if (!has_mac) length += 26; /* macaddr=xx:xx:xx:xx:xx:xx + NUL */
    size_t padded = (length + 1u + 3u) & ~(size_t)3u;
    if (padded > CHOS_CYW_NVRAM_MAX - 4u || padded + 4u > capacity)
        return CHOS_SDIO_CAPACITY;
    uint8_t *dst = output;
    size_t written = 0;
    static const uint8_t hex[] = "0123456789abcdef";
    for (unsigned i = 0; i <= count; ++i) {
        if (i == count && has_mac) break;
        int is_mac;
        if (i == count) {
            static const uint8_t key[] = "macaddr";
            for (unsigned j = 0; j < sizeof(key) - 1; ++j) dst[written++] = key[j];
            is_mac = 1;
        } else {
            const entry_t *e = &entries[i];
            for (unsigned j = 0; j < e->key_bytes; ++j) dst[written++] = b[e->key + j];
            is_mac = mac_key(b + e->key, e->key_bytes);
        }
        dst[written++] = '=';
        if (is_mac) {
            for (unsigned j = 0; j < 6; ++j) {
                if (j != 0) dst[written++] = ':';
                dst[written++] = hex[mac[j] >> 4];
                dst[written++] = hex[mac[j] & 15u];
            }
        } else {
            const entry_t *e = &entries[i];
            for (unsigned j = 0; j < e->value_bytes; ++j) dst[written++] = b[e->value + j];
        }
        dst[written++] = 0;
    }
    while (written < padded) dst[written++] = 0;
    uint32_t words = (uint32_t)(padded / 4u);
    put32(dst + padded, ((~words & UINT32_C(0xffff)) << 16) | words);
    *packed_bytes = padded + 4u;
    return CHOS_SDIO_OK;
}
