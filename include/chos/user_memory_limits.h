#ifndef CHOS_USER_MEMORY_LIMITS_H
#define CHOS_USER_MEMORY_LIMITS_H
/* Shared native/kernel bound on physical work per operation. Pure engine
 * virtual reservations have a separate32MiB cap; legacy limits stay unchanged. */
#define CHOS_USER_MAP_MAX_BYTES (1024u*1024u)
#endif
