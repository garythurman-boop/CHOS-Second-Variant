#ifndef CHOS_PCI_RESOURCES_H
#define CHOS_PCI_RESOURCES_H
#include <stddef.h>
#include "chos/boot_info.h"
/* QWORD ACPI resource stream from EFI_PCI_ROOT_BRIDGE_IO.Configuration.
 * Never retain firmware pointers or function pointers after ExitBootServices. */
static inline uint64_t chos_resource_u64(const uint8_t *p) {
    uint64_t v=0;for(unsigned i=0;i<8;++i)v|=(uint64_t)p[i]<<(i*8);return v;
}
static inline int chos_pci_capture_resources(chos_boot_info_t *info,uint32_t segment,
                                            const uint8_t *p,size_t length) {
    uint32_t original=info->pci_window_count;
    for(size_t off=0;off<length;) {
        if(p[off]==0x79) return 1;
        if(length-off<3 || p[off]!=0x8a) break;
        size_t n=(size_t)p[off+1]+((size_t)p[off+2]<<8)+3;
        if(n!=46 || n>length-off) break;
        if(p[off+3]==0) { /* System memory window */
            uint64_t bus=chos_resource_u64(p+off+14),translation=chos_resource_u64(p+off+30);
            uint64_t bytes=chos_resource_u64(p+off+38);
            if(bytes) {
                if(segment>UINT16_MAX || info->pci_window_count==CHOS_BOOT_MAX_PCI_WINDOWS ||
                   bus>UINT64_MAX-translation || bus+translation>UINT64_MAX-bytes || bus>UINT64_MAX-bytes) break;
                info->pci_windows[info->pci_window_count++]=(chos_pci_window_t){bus,bus+translation,bytes,(uint16_t)segment,0,0};
            }
        }
        off+=n;
    }
    info->pci_window_count=original;return 0;
}
#endif
