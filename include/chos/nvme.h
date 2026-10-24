#ifndef CHOS_NVME_H
#define CHOS_NVME_H

#include <stddef.h>
#include <stdint.h>
#include "chos/acpi.h"
#include "chos/blockdev.h"
#include "chos/boot_info.h"

#define CHOS_NVME_SECTOR_SIZE 512u
#define CHOS_NVME_MAX_TRANSFER_SECTORS 8u

typedef struct {
    uint16_t maximum_queue_entries;
    uint8_t doorbell_stride_log2;
    uint8_t minimum_page_size_exponent;
    uint8_t maximum_page_size_exponent;
    uint8_t nvm_command_set_supported;
} chos_nvme_capabilities_t;

/* Pure parsing helpers shared by the runtime driver and host regression tests. */
int chos_nvme_parse_capabilities(uint64_t cap, chos_nvme_capabilities_t *out);
int chos_nvme_parse_namespace_identify(const uint8_t *identify, size_t length,
                                       uint64_t *sector_count_out);

/* Experimental polled NVMe backend. The kernel must be in its identity-mapped
 * UEFI path with PMM initialized and MMU translation disabled. Only 512-byte
 * logical blocks and one namespace (NSID 1) are supported in this milestone. */
int chos_nvme_init(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                   chos_block_device_t *device_out);
const char *chos_nvme_last_error(void);
int chos_nvme_ready(void);
int chos_nvme_snapshot(uint64_t *identity,uint64_t *sectors);

#endif
