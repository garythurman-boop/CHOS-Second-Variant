#ifndef CHOS_ELF_IMAGE_H
#define CHOS_ELF_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"
#define CHOS_ELF_MAX_SEGMENTS 8u
#define CHOS_ELF_MAX_SPAN (2u*1024u*1024u)
#define CHOS_ELF_MAX_FILE (CHOS_ELF_MAX_SPAN+4096u)
typedef struct {
    uint32_t offset,virtual_offset,file_size,memory_size;
    address_space_user_permissions_t permissions;
} chos_elf_segment_t;
typedef struct {
    uint32_t entry_offset,virtual_size,segment_count;
    chos_elf_segment_t segments[CHOS_ELF_MAX_SEGMENTS];
} chos_elf_image_t;
typedef struct {
    uint32_t file_offset,file_bytes;
    address_space_user_permissions_t permissions;
} chos_elf_page_t;
/* Strict static AArch64 ET_EXEC profile for the CHOS ABI, not Linux binaries. */
int chos_elf_validate(const void *,size_t,chos_elf_image_t *);
int chos_elf_page(const chos_elf_image_t *,uint32_t,chos_elf_page_t *);
#endif
