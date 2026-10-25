#ifndef CHOS_SDIO_H
#define CHOS_SDIO_H
#include <stddef.h>
#include <stdint.h>

/* Native SDIO, not SPI. A platform must first own the regulator, pins and clock,
 * and configure a one-bit 400 kHz bus. This layer does not guess MMIO addresses.
 * Host callbacks complete synchronously within timeout_ms. This first host
 * contract is PIO-only: no borrowed caller buffer may remain device-owned. */
#define CHOS_SDIO_ADDRESS_MAX UINT32_C(0x1ffff)
#define CHOS_SDIO_TRANSFER_MAX 32768u
#define CHOS_SDIO_TIMEOUT_MS 1000u

enum {
    CHOS_SDIO_OK = 0,
    CHOS_SDIO_PENDING = 1,
    CHOS_SDIO_EMPTY = 2,
    CHOS_SDIO_INVALID = -1,
    CHOS_SDIO_IO = -2,
    CHOS_SDIO_TIMEOUT = -3,
    CHOS_SDIO_CARD_ERROR = -4,
    CHOS_SDIO_UNSUPPORTED = -5,
    CHOS_SDIO_QUARANTINED = -6,
    CHOS_SDIO_CAPACITY = -7
};
enum {
    CHOS_SDIO_R4 = 4,
    CHOS_SDIO_R5 = 5,
    CHOS_SDIO_R6 = 6,
    CHOS_SDIO_R1B = 7
};

typedef struct {
    uint32_t argument;
    void *data;
    size_t bytes;
    uint16_t block_size;
    uint16_t block_count;
    uint8_t index;
    uint8_t response;
    uint8_t write;
    uint8_t reserved;
} chos_sdio_request_t;

typedef struct {
    int (*request)(void *context, const chos_sdio_request_t *request,
                   uint32_t *response, uint32_t timeout_ms);
    uint64_t (*now_ms)(void *context);
    uint16_t maximum_transfer; /* 512..4096; must be a multiple of 4 */
    uint8_t pio_only;         /* must be 1; native DMA ownership is not implemented */
    uint8_t reserved;
} chos_sdio_host_ops_t;

typedef enum {
    CHOS_SDIO_PROBE_OCR,
    CHOS_SDIO_WAIT_POWER,
    CHOS_SDIO_ASSIGN_RCA,
    CHOS_SDIO_SELECT,
    CHOS_SDIO_READ_CCCR,
    CHOS_SDIO_READ_CAPABILITIES,
    CHOS_SDIO_READY,
    CHOS_SDIO_FAILED
} chos_sdio_state_t;

typedef struct {
    const chos_sdio_host_ops_t *host;
    void *context;
    uint64_t started_ms;
    uint32_t voltage_ocr;
    uint32_t last_response;
    uint16_t rca;
    uint16_t vendor;
    uint16_t device;
    uint16_t block_size[8];
    uint8_t functions;
    uint8_t enabled;
    uint8_t ready;
    uint8_t cccr_revision;
    uint8_t sdio_revision;
    uint8_t cccr_capabilities;
    uint8_t power_attempts;
    uint8_t identity_valid;
    chos_sdio_state_t state;
    int error;
} chos_sdio_bus_t;

/* New/zeroed state only: init rejects a state that already has a host. A failed
 * state is quarantined until the platform has
 * actually removed card power and re-established ownership. Do not reinitialize
 * a live or failed host as a retry shortcut. Pure argument encoders do no I/O. */
int chos_sdio_init(chos_sdio_bus_t *bus, const chos_sdio_host_ops_t *host,
                   void *context, uint32_t host_voltage_ocr);
int chos_sdio_probe_step(chos_sdio_bus_t *bus);
/* Common CIS MANFID, bounded to 512 tuple bytes and 32 tuples. A malformed or
 * unterminated chain quarantines the card. No guessed product identity. */
int chos_sdio_read_identity(chos_sdio_bus_t *bus, uint16_t *vendor, uint16_t *device);
int chos_sdio_cmd52_argument(int write, unsigned function, uint32_t address,
                            int read_after_write, uint8_t value, uint32_t *out);
int chos_sdio_cmd53_argument(int write, unsigned function, uint32_t address,
                            int increment, int block_mode, unsigned count,
                            uint32_t *out);
int chos_sdio_readb(chos_sdio_bus_t *bus, unsigned function, uint32_t address,
                    uint8_t *out);
int chos_sdio_writeb(chos_sdio_bus_t *bus, unsigned function, uint32_t address,
                     uint8_t value);
/* Enabling issues one write. Polling issues one read and returns PENDING until
 * the actual card IORDY bit appears. The caller supplies its own bounded timer. */
int chos_sdio_enable_function(chos_sdio_bus_t *bus, unsigned function);
int chos_sdio_poll_function(chos_sdio_bus_t *bus, unsigned function);
int chos_sdio_set_block_size(chos_sdio_bus_t *bus, unsigned function, unsigned bytes);
int chos_sdio_transfer(chos_sdio_bus_t *bus, int write, unsigned function,
                       uint32_t address, int increment, void *buffer, size_t bytes);
#endif
