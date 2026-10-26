#ifndef CHOS_ENTROPY_H
#define CHOS_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#define CHOS_ENTROPY_MAX_REQUEST 256u

/* Nonblocking service preferring VirtIO-RNG, with a finite firmware-seeded
 * HMAC-DRBG fallback when native VirtIO is absent. It never seeds from timers,
 * MAC addresses, or counters. The fallback fails closed when its boot budget expires. */
int chos_entropy_init(void);
int chos_entropy_ready(void);
/* Returns the number of bytes copied (0 when no bytes are currently available).
 * Requests are bounded to CHOS_ENTROPY_MAX_REQUEST and never block waiting for I/O. */
size_t chos_entropy_read(void *destination, size_t capacity);
const char *chos_entropy_last_error(void);

#ifdef CHOS_HOST_TEST
void chos_entropy_test_reset(void);
void chos_entropy_test_add_device(unsigned slot, uint32_t version, uint32_t device_id,
                                  uint32_t feature_version_1, uint32_t queue_max);
int chos_entropy_test_supply(const uint8_t *data, size_t length);
#endif

#endif
