#ifndef CHOS_USER_MEMORY_LIMITS_H
#define CHOS_USER_MEMORY_LIMITS_H
/* Shared native/kernel per-mapping bound. The2MiB aperture, guards,16-region
 * registry and8MiB total address-space frame quota remain independent limits. */
#define CHOS_USER_MAP_MAX_BYTES (1024u*1024u)
#endif
