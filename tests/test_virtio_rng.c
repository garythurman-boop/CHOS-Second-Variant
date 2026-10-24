#include "chos/entropy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t supplied[64], output[64];
    size_t i;

    chos_entropy_test_reset();
    assert(!chos_entropy_ready());
    assert(chos_entropy_init() == 0);
    assert(!chos_entropy_ready());
    assert(strstr(chos_entropy_last_error(), "no VirtIO RNG") != NULL);

    /* Ignore unrelated VirtIO devices and find an RNG at a later MMIO slot. */
    chos_entropy_test_add_device(1u, 2u, 2u, 1u, 8u);
    chos_entropy_test_add_device(5u, 2u, 4u, 1u, 8u);
    assert(chos_entropy_init() == 1);
    assert(chos_entropy_ready());
    assert(chos_entropy_read(output, 16u) == 0u); /* nonblocking: not ready yet */
    assert(chos_entropy_read(output, CHOS_ENTROPY_MAX_REQUEST + 1u) == 0u);
    assert(chos_entropy_read(NULL, 1u) == 0u);
    assert(chos_entropy_read(output, 0u) == 0u);

    for (i = 0u; i < sizeof(supplied); ++i) supplied[i] = (uint8_t)(i ^ 0x5au);
    assert(chos_entropy_test_supply(supplied, sizeof(supplied)) == 1);
    memset(output, 0, sizeof(output));
    assert(chos_entropy_init()==1); /* repeated init must retain pending DMA data */
    assert(chos_entropy_read(output, 13u) == 13u);
    assert(memcmp(output, supplied, 13u) == 0);
    assert(chos_entropy_read(output + 13u, sizeof(output) - 13u) == sizeof(output) - 13u);
    assert(memcmp(output, supplied, sizeof(output)) == 0);
    assert(chos_entropy_read(output, 1u) == 0u);

    /* A second request is posted after consumption; data does not get reused. */
    assert(chos_entropy_test_supply(supplied, 7u) == 1);
    assert(chos_entropy_read(output, 7u) == 7u);
    assert(memcmp(output, supplied, 7u) == 0);

    chos_entropy_test_reset();
    chos_entropy_test_add_device(0u, 1u, 4u, 1u, 8u);
    assert(chos_entropy_init() == 0); /* legacy transport is deliberately refused */
    assert(!chos_entropy_ready());

    chos_entropy_test_reset();
    chos_entropy_test_add_device(2u, 2u, 4u, 0u, 8u);
    assert(chos_entropy_init() == 0); /* VERSION_1 feature must be present */
    assert(!chos_entropy_ready());

    chos_entropy_test_reset();
    chos_entropy_test_add_device(2u, 2u, 4u, 1u, 0u);
    assert(chos_entropy_init() == 0); /* an absent queue is not accepted */
    assert(!chos_entropy_ready());

    puts("VirtIO RNG: bounded nonblocking reads, feature/queue validation and no fabricated entropy passed");
    return 0;
}
