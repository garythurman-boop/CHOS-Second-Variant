#ifndef CHOS_MEMORY_API_H
#define CHOS_MEMORY_API_H
/* MEMORY_CONTROL64: existing anonymous owned ranges/reservations; never code/devices.
 * Uses MAP_MEMORY's permission class. UNMAP_RANGE65 uses UNMAP_MEMORY's class.
 * No executable permissions, fixed addresses, file or shared mappings. */
#define CHOS_MEMORY_PROTECT_NONE 0u
#define CHOS_MEMORY_PROTECT_READ 1u
#define CHOS_MEMORY_PROTECT_RW 3u
#define CHOS_MEMORY_DISCARD 256u
#define CHOS_MEMORY_VALIDATE 257u
/* Explicit engine-only virtual reservation; no physical backing until commit.
 * MAP_MEMORY35: x0=bytes,x1=RESERVE,x2=power-of-two alignment. */
#define CHOS_MEMORY_MAP_RESERVE 1u
#define CHOS_MEMORY_DECOMMIT 258u
#endif
