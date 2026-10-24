#ifndef CHOS_CLOCK_H
#define CHOS_CLOCK_H
#include "chos/boot_info.h"
/* Called only after boot information validation; never invokes firmware services. */
int chos_clock_init_uefi(const chos_boot_info_t *info);
/* Direct boot binds the PL031 discovered in its validated FDT. No implicit address. */
int chos_clock_use_pl031(uintptr_t base);
const char *chos_clock_source(void);
#endif
