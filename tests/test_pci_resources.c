#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "chos/pci_resources.h"
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(v>>(8*i));}
int main(void){
    chos_boot_info_t b={0};uint8_t r[47]={0x8a,43,0,0};
    put64(r+14,0x80000000);put64(r+30,0x10000000);put64(r+38,0x100000);r[46]=0x79;
    assert(chos_pci_capture_resources(&b,2,r,sizeof(r)));
    assert(b.pci_window_count==1 && b.pci_windows[0].cpu_base==0x90000000 && b.pci_windows[0].segment==2);
    for(size_t n=0;n<sizeof(r);++n) {b.pci_window_count=0;assert(!chos_pci_capture_resources(&b,0,r,n));assert(!b.pci_window_count);}
    b.pci_window_count=0;put64(r+14,UINT64_MAX);assert(!chos_pci_capture_resources(&b,0,r,sizeof(r)));
    put64(r+14,0x80000000);b.pci_window_count=CHOS_BOOT_MAX_PCI_WINDOWS;assert(!chos_pci_capture_resources(&b,0,r,sizeof(r)));
    b.pci_window_count=0;r[1]=42;assert(!chos_pci_capture_resources(&b,0,r,sizeof(r)));
    puts("EFI PCI resources: translated windows, truncation, overflow, capacity and malformed descriptor passed");
}
