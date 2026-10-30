#ifndef CHOS_IMAGE_MEMORY_POLICY_H
#define CHOS_IMAGE_MEMORY_POLICY_H
#include <stdint.h>
/* Caller owns exactly ceil(image_pages/64) words in each mask. Validation does
 * not change masks or output indices on errors. Mapped-page ownership and AS
 * data-page descriptors must additionally be checked under their kernel locks. */
int chos_image_memory_range(uint64_t image_base,uint32_t image_pages,const uint64_t *mapped,
 const uint64_t *inaccessible,uint64_t address,uint64_t bytes,uint64_t operation,uint32_t *first,uint32_t *count);
#endif
