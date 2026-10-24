#ifndef CHOS_XHCI_COMPAT_H
#define CHOS_XHCI_COMPAT_H
#include <stdint.h>
/* Explicit device-specific deviations stay in one bounded table. They do not
 * bypass BAR bounds, register-block checks, reset, ownership or DMA validation.
 * VMware's compact HCIVERSION is recorded by the independently tested Test-CHOS
 * Fusion observations. Standard controllers continue to use the standard BCD. */
static inline uint16_t chos_xhci_version(uint16_t vendor,uint16_t device,uint16_t reported) {
    static const struct {uint16_t vendor,device,reported,version;} quirks[]={
        {0x15ad,0x077a,0x0012,0x0120}
    };
    if(reported>=0x0100 && reported<=0x0120)return reported;
    for(unsigned i=0;i<sizeof(quirks)/sizeof(quirks[0]);++i)
        if(vendor==quirks[i].vendor && device==quirks[i].device && reported==quirks[i].reported)
            return quirks[i].version;
    return 0;
}
#endif
