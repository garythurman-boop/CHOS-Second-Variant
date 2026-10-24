#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/nvme.h"

static void write_le64(uint8_t *p, uint64_t v) {
    unsigned i; for (i = 0u; i < 8u; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}

int main(void) {
    chos_nvme_capabilities_t caps;
    uint8_t ns[4096] = {0};
    uint64_t sectors = 0u;
    uint64_t cap = UINT64_C(0x000000000000003f) | (UINT64_C(0x1) << 37u);
    assert(chos_nvme_parse_capabilities(cap, &caps));
    assert(caps.maximum_queue_entries == 64u);
    assert(caps.doorbell_stride_log2 == 0u && caps.minimum_page_size_exponent == 0u);
    assert(!chos_nvme_parse_capabilities(cap & ~(UINT64_C(1) << 37u), &caps));
    assert(!chos_nvme_parse_capabilities(cap | (UINT64_C(1) << 48u), &caps));
    assert(!chos_nvme_parse_capabilities((cap & ~(UINT64_C(0xf) << 32u)) | (UINT64_C(9) << 32u), &caps));

    write_le64(&ns[0], UINT64_C(8388608)); /* 4 GiB at 512-byte sectors */
    ns[25] = 0u; ns[26] = 0u;
    ns[128] = 0u; ns[129] = 0u; ns[130] = 9u;
    assert(chos_nvme_parse_namespace_identify(ns, sizeof(ns), &sectors));
    assert(sectors == UINT64_C(8388608));
    ns[130] = 12u; /* 4 KiB LBA not supported by the 512-byte block contract */
    assert(!chos_nvme_parse_namespace_identify(ns, sizeof(ns), &sectors));
    ns[130] = 9u; ns[128] = 8u; /* metadata present */
    assert(!chos_nvme_parse_namespace_identify(ns, sizeof(ns), &sectors));
    ns[128] = ns[129] = 0u; ns[26] = 0x10u; /* extended LBA metadata */
    assert(!chos_nvme_parse_namespace_identify(ns, sizeof(ns), &sectors));
    ns[26] = 0u; write_le64(&ns[0], 0u);
    assert(!chos_nvme_parse_namespace_identify(ns, sizeof(ns), &sectors));
    assert(!chos_nvme_parse_namespace_identify(ns, 191u, &sectors));
    puts("NVMe parser: CAP constraints, 4 KiB page support, queue depth, 512-byte namespace, metadata rejection and malformed identify data passed");
    return 0;
}
