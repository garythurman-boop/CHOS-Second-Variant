#include "chos/sdio.h"

#define R5_ERRORS UINT32_C(0xcb00)
#define OCR_VOLTAGE UINT32_C(0x00ff8000)
#define OCR_READY UINT32_C(0x80000000)
#define OCR_MEMORY UINT32_C(0x08000000)
#define R6_ERRORS UINT32_C(0xe000)
#define R1_ERRORS UINT32_C(0xfdffe008)

static int boolean(int v) { return v == 0 || v == 1; }
static int fail(chos_sdio_bus_t *bus, int error) {
    bus->state = CHOS_SDIO_FAILED;
    bus->error = error;
    bus->ready = 0;
    return error;
}
static int request(chos_sdio_bus_t *bus, const chos_sdio_request_t *req,
                   uint32_t *response) {
    if (bus->state == CHOS_SDIO_FAILED) return CHOS_SDIO_QUARANTINED;
    uint32_t reply = 0;
    uint64_t started = bus->host->now_ms(bus->context);
    int rc = bus->host->request(bus->context, req, &reply, CHOS_SDIO_TIMEOUT_MS);
    bus->last_response = reply;
    if (rc != CHOS_SDIO_OK)
        return fail(bus, rc == CHOS_SDIO_TIMEOUT ? rc : CHOS_SDIO_IO);
    if (bus->host->now_ms(bus->context) - started >= CHOS_SDIO_TIMEOUT_MS)
        return fail(bus, CHOS_SDIO_TIMEOUT);
    if (req->response == CHOS_SDIO_R5 && (reply & R5_ERRORS))
        return fail(bus, CHOS_SDIO_CARD_ERROR);
    *response = reply;
    return CHOS_SDIO_OK;
}
static int direct(chos_sdio_bus_t *bus, int write, unsigned fn, uint32_t addr,
                  uint8_t value, uint8_t *out) {
    chos_sdio_request_t req = {0};
    uint32_t response;
    int rc = chos_sdio_cmd52_argument(write, fn, addr, 0, value, &req.argument);
    if (rc != CHOS_SDIO_OK) return rc;
    req.index = 52;
    req.response = CHOS_SDIO_R5;
    rc = request(bus, &req, &response);
    if (rc == CHOS_SDIO_OK && out != NULL) *out = (uint8_t)response;
    return rc;
}
static int function_valid(const chos_sdio_bus_t *bus, unsigned function) {
    return bus != NULL && bus->host != NULL && bus->state == CHOS_SDIO_READY &&
        function <= bus->functions &&
        (function == 0 || (bus->ready & (1u << function)) != 0);
}

int chos_sdio_cmd52_argument(int write, unsigned fn, uint32_t addr,
                            int raw, uint8_t value, uint32_t *out) {
    if (!boolean(write) || !boolean(raw) || fn > 7 ||
        addr > CHOS_SDIO_ADDRESS_MAX || (raw && !write) || out == NULL)
        return CHOS_SDIO_INVALID;
    *out = ((uint32_t)write << 31) | ((uint32_t)fn << 28) |
        ((uint32_t)raw << 27) | (addr << 9) | value;
    return CHOS_SDIO_OK;
}
int chos_sdio_cmd53_argument(int write, unsigned fn, uint32_t addr,
                            int increment, int block, unsigned count,
                            uint32_t *out) {
    if (!boolean(write) || !boolean(increment) || !boolean(block) || fn > 7 ||
        addr > CHOS_SDIO_ADDRESS_MAX || out == NULL || count == 0 ||
        count > (block ? 511u : 512u)) return CHOS_SDIO_INVALID;
    /* 0 means exactly 512 bytes in byte mode; zero block count would mean an
     * indefinite transfer and is deliberately never generated. */
    *out = ((uint32_t)write << 31) | ((uint32_t)fn << 28) |
        ((uint32_t)block << 27) | ((uint32_t)increment << 26) |
        (addr << 9) | (count & 511u);
    return CHOS_SDIO_OK;
}

int chos_sdio_init(chos_sdio_bus_t *bus, const chos_sdio_host_ops_t *host,
                   void *context, uint32_t voltage) {
    if (bus == NULL || host == NULL || host->request == NULL ||
        host->now_ms == NULL || host->pio_only != 1 ||
        host->maximum_transfer < 512 || host->maximum_transfer > 4096 ||
        (host->maximum_transfer & 3u) || (voltage & OCR_VOLTAGE) == 0 ||
        (voltage & ~OCR_VOLTAGE) != 0 || bus->host != NULL) return CHOS_SDIO_INVALID;
    *bus = (chos_sdio_bus_t){0};
    bus->host = host;
    bus->context = context;
    bus->voltage_ocr = voltage;
    bus->started_ms = host->now_ms(context);
    bus->state = CHOS_SDIO_PROBE_OCR;
    return CHOS_SDIO_OK;
}

int chos_sdio_read_identity(chos_sdio_bus_t *bus, uint16_t *vendor, uint16_t *device) {
    if (bus != NULL && bus->state == CHOS_SDIO_FAILED) return CHOS_SDIO_QUARANTINED;
    if (!function_valid(bus, 0) || vendor == NULL || device == NULL || vendor == device)
        return CHOS_SDIO_INVALID;
    if (bus->identity_valid) {
        *vendor = bus->vendor; *device = bus->device;
        return CHOS_SDIO_OK;
    }
    uint32_t pointer = 0;
    uint64_t started = bus->host->now_ms(bus->context);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t byte;
        int rc = direct(bus, 0, 0, 9u + i, 0, &byte);
        if (rc != CHOS_SDIO_OK) return rc;
        pointer |= (uint32_t)byte << (i * 8u);
    }
    if (pointer < 0x100 || pointer > CHOS_SDIO_ADDRESS_MAX - 1u)
        return fail(bus, CHOS_SDIO_CARD_ERROR);
    unsigned consumed = 0, tuples = 0;
    uint16_t found_vendor = 0, found_device = 0;
    int found = 0;
    while (consumed < 512 && tuples < 32) {
        if (bus->host->now_ms(bus->context) - started >= CHOS_SDIO_TIMEOUT_MS)
            return fail(bus, CHOS_SDIO_TIMEOUT);
        uint8_t code, length;
        int rc = direct(bus, 0, 0, pointer, 0, &code);
        if (rc != CHOS_SDIO_OK) return rc;
        if (bus->host->now_ms(bus->context) - started >= CHOS_SDIO_TIMEOUT_MS)
            return fail(bus, CHOS_SDIO_TIMEOUT);
        ++pointer; ++consumed;
        if (code == 0xff) {
            if (!found || found_vendor == 0 || found_vendor == 0xffff)
                return fail(bus, CHOS_SDIO_CARD_ERROR);
            bus->vendor = found_vendor; bus->device = found_device;
            bus->identity_valid = 1;
            *vendor = found_vendor; *device = found_device;
            return CHOS_SDIO_OK;
        }
        if (pointer > CHOS_SDIO_ADDRESS_MAX) return fail(bus, CHOS_SDIO_CARD_ERROR);
        if (code == 0) continue; /* NULL tuple has no link byte */
        ++tuples;
        rc = direct(bus, 0, 0, pointer++, 0, &length);
        if (rc != CHOS_SDIO_OK) return rc;
        ++consumed;
        if (length > 512u - consumed || length > CHOS_SDIO_ADDRESS_MAX - pointer + 1u)
            return fail(bus, CHOS_SDIO_CARD_ERROR);
        if (code == 0x20) {
            if (length != 4 || found) return fail(bus, CHOS_SDIO_CARD_ERROR);
            uint8_t id[4];
            for (unsigned i = 0; i < 4; ++i) {
                rc = direct(bus, 0, 0, pointer + i, 0, &id[i]);
                if (rc != CHOS_SDIO_OK) return rc;
            }
            found_vendor = (uint16_t)id[0] | ((uint16_t)id[1] << 8);
            found_device = (uint16_t)id[2] | ((uint16_t)id[3] << 8);
            found = 1;
        }
        pointer += length;
        consumed += length;
        if (pointer > CHOS_SDIO_ADDRESS_MAX) return fail(bus, CHOS_SDIO_CARD_ERROR);
    }
    return fail(bus, CHOS_SDIO_CARD_ERROR);
}
int chos_sdio_probe_step(chos_sdio_bus_t *bus) {
    if (bus == NULL || bus->host == NULL) return CHOS_SDIO_INVALID;
    if (bus->state == CHOS_SDIO_FAILED) return CHOS_SDIO_QUARANTINED;
    if (bus->state == CHOS_SDIO_READY) return CHOS_SDIO_OK;
    if (bus->host->now_ms(bus->context) - bus->started_ms >= CHOS_SDIO_TIMEOUT_MS)
        return fail(bus, CHOS_SDIO_TIMEOUT);
    chos_sdio_request_t req = {0};
    uint32_t response;
    int rc;
    switch (bus->state) {
    case CHOS_SDIO_PROBE_OCR:
    case CHOS_SDIO_WAIT_POWER:
        req.index = 5;
        req.response = CHOS_SDIO_R4;
        if (bus->state == CHOS_SDIO_WAIT_POWER) req.argument = bus->voltage_ocr;
        rc = request(bus, &req, &response);
        if (rc != CHOS_SDIO_OK) return rc;
        if (bus->state == CHOS_SDIO_PROBE_OCR) {
            bus->functions = (uint8_t)((response >> 28) & 7u);
            bus->voltage_ocr &= response & OCR_VOLTAGE;
            /* Only dedicated SDIO cards are handled, never SD-memory media. */
            if (bus->functions == 0 || bus->voltage_ocr == 0 || (response & OCR_MEMORY))
                return fail(bus, CHOS_SDIO_UNSUPPORTED);
            bus->state = CHOS_SDIO_WAIT_POWER;
        } else {
            if (((response >> 28) & 7u) != bus->functions || (response & OCR_MEMORY) ||
                (response & bus->voltage_ocr) == 0)
                return fail(bus, CHOS_SDIO_CARD_ERROR);
            if (response & OCR_READY) bus->state = CHOS_SDIO_ASSIGN_RCA;
            else if (++bus->power_attempts >= 32) return fail(bus, CHOS_SDIO_TIMEOUT);
        }
        return CHOS_SDIO_PENDING;
    case CHOS_SDIO_ASSIGN_RCA:
        req.index = 3;
        req.response = CHOS_SDIO_R6;
        rc = request(bus, &req, &response);
        if (rc != CHOS_SDIO_OK) return rc;
        bus->rca = (uint16_t)(response >> 16);
        if (bus->rca == 0 || (response & R6_ERRORS)) return fail(bus, CHOS_SDIO_CARD_ERROR);
        bus->state = CHOS_SDIO_SELECT;
        return CHOS_SDIO_PENDING;
    case CHOS_SDIO_SELECT:
        req.index = 7;
        req.argument = (uint32_t)bus->rca << 16;
        req.response = CHOS_SDIO_R1B;
        rc = request(bus, &req, &response);
        if (rc != CHOS_SDIO_OK) return rc;
        if (response & R1_ERRORS) return fail(bus, CHOS_SDIO_CARD_ERROR);
        bus->state = CHOS_SDIO_READ_CCCR;
        return CHOS_SDIO_PENDING;
    case CHOS_SDIO_READ_CCCR: {
        uint8_t revision;
        rc = direct(bus, 0, 0, 0, 0, &revision);
        if (rc != CHOS_SDIO_OK) return rc;
        bus->cccr_revision = revision & 15u;
        bus->sdio_revision = revision >> 4;
        if (bus->cccr_revision > 3 || bus->sdio_revision > 4)
            return fail(bus, CHOS_SDIO_UNSUPPORTED);
        bus->state = CHOS_SDIO_READ_CAPABILITIES;
        return CHOS_SDIO_PENDING;
    }
    case CHOS_SDIO_READ_CAPABILITIES: {
        uint8_t capabilities;
        rc = direct(bus, 0, 0, 8, 0, &capabilities);
        if (rc != CHOS_SDIO_OK) return rc;
        bus->cccr_capabilities = capabilities;
        bus->state = CHOS_SDIO_READY;
        return CHOS_SDIO_OK;
    }
    default: return fail(bus, CHOS_SDIO_INVALID);
    }
}

int chos_sdio_readb(chos_sdio_bus_t *bus, unsigned fn, uint32_t addr, uint8_t *out) {
    if (bus != NULL && bus->state == CHOS_SDIO_FAILED) return CHOS_SDIO_QUARANTINED;
    if (!function_valid(bus, fn) || out == NULL) return CHOS_SDIO_INVALID;
    return direct(bus, 0, fn, addr, 0, out);
}
int chos_sdio_writeb(chos_sdio_bus_t *bus, unsigned fn, uint32_t addr, uint8_t value) {
    if (bus != NULL && bus->state == CHOS_SDIO_FAILED) return CHOS_SDIO_QUARANTINED;
    if (!function_valid(bus, fn)) return CHOS_SDIO_INVALID;
    return direct(bus, 1, fn, addr, value, NULL);
}
int chos_sdio_enable_function(chos_sdio_bus_t *bus, unsigned fn) {
    if (!function_valid(bus, 0) || fn == 0 || fn > bus->functions)
        return CHOS_SDIO_INVALID;
    uint8_t enabled;
    int rc = direct(bus, 0, 0, 2, 0, &enabled);
    if (rc != CHOS_SDIO_OK) return rc;
    rc = direct(bus, 1, 0, 2, (uint8_t)(enabled | (1u << fn)), NULL);
    if (rc == CHOS_SDIO_OK) bus->enabled |= (uint8_t)(1u << fn);
    return rc;
}
int chos_sdio_poll_function(chos_sdio_bus_t *bus, unsigned fn) {
    if (!function_valid(bus, 0) || fn == 0 || fn > bus->functions ||
        !(bus->enabled & (1u << fn))) return CHOS_SDIO_INVALID;
    uint8_t ready;
    int rc = direct(bus, 0, 0, 3, 0, &ready);
    if (rc != CHOS_SDIO_OK) return rc;
    bus->ready = ready & bus->enabled;
    return (bus->ready & (1u << fn)) ? CHOS_SDIO_OK : CHOS_SDIO_PENDING;
}
int chos_sdio_set_block_size(chos_sdio_bus_t *bus, unsigned fn, unsigned bytes) {
    if (!function_valid(bus, fn) || fn == 0 || bytes == 0 || bytes > 512)
        return CHOS_SDIO_INVALID;
    int rc = direct(bus, 1, 0, fn * 256u + 16u, (uint8_t)bytes, NULL);
    if (rc != CHOS_SDIO_OK) return rc;
    rc = direct(bus, 1, 0, fn * 256u + 17u, (uint8_t)(bytes >> 8), NULL);
    if (rc == CHOS_SDIO_OK) bus->block_size[fn] = (uint16_t)bytes;
    return rc;
}
int chos_sdio_transfer(chos_sdio_bus_t *bus, int write, unsigned fn,
                       uint32_t addr, int increment, void *buffer, size_t bytes) {
    if (bus != NULL && bus->state == CHOS_SDIO_FAILED) return CHOS_SDIO_QUARANTINED;
    if (!function_valid(bus, fn) || fn == 0 || !boolean(write) || !boolean(increment) ||
        buffer == NULL || bytes == 0 || bytes > CHOS_SDIO_TRANSFER_MAX ||
        addr > CHOS_SDIO_ADDRESS_MAX ||
        (increment && bytes - 1 > CHOS_SDIO_ADDRESS_MAX - addr))
        return CHOS_SDIO_INVALID;
    uint8_t *data = buffer;
    uint64_t started = bus->host->now_ms(bus->context);
    unsigned commands = 0;
    while (bytes != 0) {
        if (++commands > 64 || bus->host->now_ms(bus->context) - started >= CHOS_SDIO_TIMEOUT_MS)
            return fail(bus, CHOS_SDIO_TIMEOUT);
        chos_sdio_request_t req = {0};
        uint32_t response;
        unsigned count;
        int block = 0;
        size_t chunk = bytes;
        if ((bus->cccr_capabilities & 2u) && bus->block_size[fn] != 0 &&
            chunk >= bus->block_size[fn]) {
            if (chunk > bus->host->maximum_transfer) chunk = bus->host->maximum_transfer;
            count = (unsigned)(chunk / bus->block_size[fn]);
            if (count > 511) count = 511;
            chunk = (size_t)count * bus->block_size[fn];
            if (chunk >= 512 || bytes < 512) {
                req.block_size = bus->block_size[fn];
                req.block_count = (uint16_t)count;
                block = 1;
            }
        }
        if (!block) {
            chunk = bytes;
            if (chunk > 512) chunk = 512;
            count = (unsigned)chunk;
            req.block_size = (uint16_t)chunk;
            req.block_count = 1;
        }
        (void)chos_sdio_cmd53_argument(write, fn, addr, increment, block, count, &req.argument);
        req.index = 53;
        req.response = CHOS_SDIO_R5;
        req.write = (uint8_t)write;
        req.bytes = chunk;
        req.data = data;
        int rc = request(bus, &req, &response);
        if (rc != CHOS_SDIO_OK) return rc;
        if (bus->host->now_ms(bus->context) - started >= CHOS_SDIO_TIMEOUT_MS)
            return fail(bus, CHOS_SDIO_TIMEOUT);
        data += chunk;
        bytes -= chunk;
        if (increment) addr += (uint32_t)chunk;
    }
    return CHOS_SDIO_OK;
}
