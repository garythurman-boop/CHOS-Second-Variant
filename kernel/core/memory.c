#include <stddef.h>
#include <stdint.h>

/* Freestanding C memory primitives. Clang may lower aggregate assignments to
 * these symbols even with -fno-builtin, so the kernel provides its own bounded,
 * byte-exact implementations rather than depending on a host C runtime. */
void *memcpy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t i;
    for (i = 0u; i < length; ++i) out[i] = in[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out == in || length == 0u) return destination;
    if ((uintptr_t)out < (uintptr_t)in || (uintptr_t)out - (uintptr_t)in >= length) {
        size_t i;
        for (i = 0u; i < length; ++i) out[i] = in[i];
    } else {
        while (length != 0u) { --length; out[length] = in[length]; }
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    size_t i;
    for (i = 0u; i < length; ++i) out[i] = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t i;
    for (i = 0u; i < length; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}
