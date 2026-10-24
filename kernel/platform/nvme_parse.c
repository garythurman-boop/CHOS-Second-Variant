#include <stddef.h>
#include <stdint.h>
#include "chos/nvme.h"

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint64_t read_le64(const uint8_t *p) {
    uint64_t v = 0u;
    unsigned i;
    for (i = 0u; i < 8u; ++i) v |= (uint64_t)p[i] << (i * 8u);
    return v;
}

int chos_nvme_parse_capabilities(uint64_t cap, chos_nvme_capabilities_t *out) {
    chos_nvme_capabilities_t parsed;
    uint32_t mqes = (uint32_t)(cap & UINT64_C(0xffff));
    uint32_t dstrd = (uint32_t)((cap >> 32u) & 0x0fu);
    uint32_t mpsmin = (uint32_t)((cap >> 48u) & 0x0fu);
    uint32_t mpsmax = (uint32_t)((cap >> 52u) & 0x0fu);
    /* CHOS presently allocates 4 KiB pages and supports the NVM command set. */
    if (out == NULL || (cap & (UINT64_C(1) << 37u)) == 0u || mpsmin != 0u ||
        mpsmax < mpsmin || mqes < 7u || dstrd > 8u) return 0;
    parsed.maximum_queue_entries = (uint16_t)(mqes + 1u > 65535u ? 65535u : mqes + 1u);
    parsed.doorbell_stride_log2 = (uint8_t)dstrd;
    parsed.minimum_page_size_exponent = (uint8_t)mpsmin;
    parsed.maximum_page_size_exponent = (uint8_t)mpsmax;
    parsed.nvm_command_set_supported = 1u;
    *out = parsed;
    return 1;
}

int chos_nvme_parse_namespace_identify(const uint8_t *identify, size_t length,
                                       uint64_t *sector_count_out) {
    uint64_t nsze;
    uint8_t nlbaf, flbas, lba_shift;
    uint16_t metadata_size;
    size_t desc;
    if (identify == NULL || sector_count_out == NULL || length < 192u) return 0;
    nsze = read_le64(identify);
    nlbaf = identify[25u];
    flbas = identify[26u];
    if (nsze == 0u || nlbaf > 15u || (flbas & 0x0fu) > nlbaf) return 0;
    /* Extended LBA metadata is not yet supported. Fail closed rather than
     * accidentally expose a sector size that the block layer misinterprets. */
    if ((flbas & 0x30u) != 0u) return 0;
    desc = 128u + (size_t)(flbas & 0x0fu) * 4u;
    if (desc + 4u > length) return 0;
    metadata_size = read_le16(&identify[desc]);
    lba_shift = identify[desc + 2u];
    if (metadata_size != 0u || lba_shift != 9u) return 0;
    *sector_count_out = nsze;
    return 1;
}
