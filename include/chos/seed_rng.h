#ifndef CHOS_SEED_RNG_H
#define CHOS_SEED_RNG_H
#include <stdint.h>
#include <stddef.h>
#define CHOS_SEED_RNG_BYTES 64u
#define CHOS_SEED_RNG_CALL_LIMIT 4096u
/* Accepts exactly64 writable bytes, erasing them on success or failure; other sizes are refused untouched. No reseed from software clocks. */
int chos_seed_rng_init(uint8_t *seed,size_t bytes);
int chos_seed_rng_ready(void);
size_t chos_seed_rng_read(void *,size_t);
#endif
