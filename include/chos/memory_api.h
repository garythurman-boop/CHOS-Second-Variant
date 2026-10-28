#ifndef CHOS_MEMORY_API_H
#define CHOS_MEMORY_API_H
/* MEMORY_CONTROL64: existing anonymous owned ranges only; never code/devices.
 * Uses MAP_MEMORY's permission class. UNMAP_RANGE65 uses UNMAP_MEMORY's class.
 * No executable permissions, fixed addresses, file or shared mappings. */
#define CHOS_MEMORY_PROTECT_NONE 0u
#define CHOS_MEMORY_PROTECT_READ 1u
#define CHOS_MEMORY_PROTECT_RW 3u
#define CHOS_MEMORY_DISCARD 256u
#define CHOS_MEMORY_VALIDATE 257u
#endif
