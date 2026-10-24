#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/platform.h"

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__linux__) && defined(MAP_FIXED_NOREPLACE)
static void store_be32(volatile uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
#endif

int main(void) {
    assert(platform_dtb_address() == (uintptr_t)0x40000000u);
    platform_set_boot_dtb_hint(0u);
    platform_set_boot_dtb_hint((uintptr_t)0x1234u); /* Out of range: must not dereference. */
    assert(platform_dtb_address() == (uintptr_t)0x40000000u);
#if defined(__linux__) && defined(MAP_FIXED_NOREPLACE)
    {
        void *candidate = mmap((void *)(uintptr_t)0x40010000u, 4096u,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (candidate != MAP_FAILED) {
            volatile uint8_t *bytes = (volatile uint8_t *)candidate;
            store_be32(bytes, 0xd00dfeedu);
            store_be32(bytes + 4u, 40u);
            platform_set_boot_dtb_hint((uintptr_t)candidate);
            assert(platform_dtb_address() == (uintptr_t)candidate);
            munmap(candidate, 4096u);
        }
    }
#endif
    puts("platform boot handoff: invalid hint falls back safely; valid in-range FDT hints are accepted where host mapping permits");
    return 0;
}
