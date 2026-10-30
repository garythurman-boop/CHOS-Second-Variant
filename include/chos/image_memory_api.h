#ifndef CHOS_IMAGE_MEMORY_API_H
#define CHOS_IMAGE_MEMORY_API_H
#include "chos/memory_api.h"
/* IMAGE_PROTECT78: current process image data pages only,1..1MiB,aligned base.
 * Length rounds up to whole pages. READ/NONE are the only operations. No write
 * or executable permission can be added. NONE cannot later become readable.
 * Anonymous mappings keep MEMORY_CONTROL64's existing reversible contract.
 * No allocations/unmaps,stack/device/kernel pages or foreign image handles. */
#define CHOS_IMAGE_PROTECT_MAX_BYTES (1024u*1024u)
#endif
