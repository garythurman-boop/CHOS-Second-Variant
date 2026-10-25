#ifndef CHOS_CYW_SDIO_H
#define CHOS_CYW_SDIO_H
#include "chos/sdio.h"

#define CHOS_CYW_BACKPLANE_WINDOW 32768u
#define CHOS_CYW_SDPCM_HEADER 12u
#define CHOS_CYW_SDPCM_MAX 8192u
#define CHOS_CYW_NVRAM_MAX 64000u
#define CHOS_CYW_NVRAM_ENTRIES 192u

typedef struct {
    chos_sdio_bus_t *bus;
    uint32_t window;
    uint8_t window_valid;
} chos_cyw_sdio_t;
typedef struct {
    uint16_t chip;
    uint8_t revision;
    uint8_t package;
    uint8_t type;
} chos_cyw_chip_id_t;
typedef struct {
    const uint8_t *payload;
    size_t payload_bytes;
    uint16_t frame_bytes;
    uint8_t sequence;
    uint8_t channel;
    uint8_t next_length_16;
    uint8_t flow_control;
    uint8_t transmit_limit;
} chos_cyw_sdpcm_frame_t;

/* This is a bus/protocol implementation, not a registered wireless adapter.
 * The platform must really enumerate the common CIS, enable F1 and configure its
 * 64-byte block size first. An unmatched manufacturer/product is refused before
 * any backplane register writes. Caller serializes this bus/window owner. No
 * firmware, radio enable, scan or association is synthesized. */
int chos_cyw_sdio_init(chos_cyw_sdio_t *radio, chos_sdio_bus_t *bus);
int chos_cyw_backplane_read32(chos_cyw_sdio_t *radio, uint32_t address, uint32_t *out);
int chos_cyw_backplane_write32(chos_cyw_sdio_t *radio, uint32_t address, uint32_t value);
int chos_cyw_backplane_transfer(chos_cyw_sdio_t *radio, int write, uint32_t address,
                               void *buffer, size_t bytes);
/* Reads the physical chipcommon ID. Only the explicitly selected 4345 rev6
 * profile is accepted as CYW43455; other revisions require an audited firmware
 * mapping. Parsed identity is returned even when the profile is unsupported. */
int chos_cyw_read_chip_id(chos_cyw_sdio_t *radio, chos_cyw_chip_id_t *out);
int chos_cyw_sdpcm_decode(const void *buffer, size_t bytes, chos_cyw_sdpcm_frame_t *out);
int chos_cyw_sdpcm_encode(void *buffer, size_t capacity, unsigned channel,
                          uint8_t sequence, const void *payload, size_t payload_bytes,
                          size_t *transfer_bytes);
/* Pack a verified, board-specific text file into the firmware NUL-separated
 * format and length token. Calibration/country entries are preserved. No
 * defaults are invented. A real platform-provided unicast MAC replaces the
 * template MAC, which must never be reused on all boards. No radio I/O occurs.
 * Conflicting duplicates, multi-device formats and malformed text are rejected.
 * Failures leave output and *packed_bytes unchanged. Input/output must not alias. */
int chos_cyw_nvram_pack(const void *text, size_t bytes, const uint8_t board_mac[6],
                        void *output, size_t capacity, size_t *packed_bytes);
#endif
