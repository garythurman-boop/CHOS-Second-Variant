#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/platform/virtio_transport.c"
static uint32_t cfg[64];
static uint8_t bar_mem[8192] __attribute__((aligned(4096)));
static unsigned mappings,enables;
int chos_pci_read_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t s,uint8_t bus,uint8_t dev,uint8_t fn,uint16_t off,uint32_t *v) {
    (void)b;(void)a;(void)s;(void)bus;(void)dev;(void)fn;
    if(off>=sizeof(cfg))return 0;
    *v=cfg[off/4];return 1;
}
int chos_pci_write_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t s,uint8_t bus,uint8_t dev,uint8_t fn,uint16_t off,uint32_t v) {
    (void)b;(void)a;(void)s;(void)bus;(void)dev;(void)fn;
    if(off>=sizeof(cfg))return 0;
    cfg[off/4]=v;return 1;
}
int chos_pci_map_bar(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d,unsigned index,uint64_t *base,uint64_t *size) {
    (void)b;(void)a;(void)d;++mappings;if(index!=4)return 0;
    *base=(uintptr_t)bar_mem+(d->device?4096u:0u);*size=4096;return 1;
}
int chos_pci_enable_memory_busmaster(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d) {
    (void)b;(void)a;(void)d;++enables;return 1;
}
static void cap(unsigned at,unsigned next,unsigned type,unsigned offset,unsigned length) {
    cfg[at/4]=9u|(next<<8)|(16u<<16)|(type<<24);
    cfg[at/4+1]=4;cfg[at/4+2]=offset;cfg[at/4+3]=length;
}
static void reset(void) {
    aperture=0;pci_mode=0;transport_count=0;mappings=enables=0;
    memset(cfg,0,sizeof(cfg));memset(bar_mem,0,sizeof(bar_mem));
    cfg[0x34/4]=0x40;
    cap(0x40,0x50,1,0,64);cap(0x50,0x68,2,256,64);
    cfg[0x50/4]|=20u<<16;cfg[0x60/4]=4;
    cap(0x68,0x78,3,512,1);cap(0x78,0,4,768,64);
    w16((uintptr_t)bar_mem+18,2);w16((uintptr_t)bar_mem+24,64);
}
int main(void) {
    chos_boot_info_t boot={0};chos_acpi_info_t acpi={.valid=1};
    chos_pci_device_t dev={.vendor_id=0x1af4,.device_id=0x1042};
    reset();assert(chos_virtio_use_pci(&boot,&acpi,&dev,1)==1);assert(mappings==1 && enables==1);
    uintptr_t h=chos_virtio_aperture();assert(chos_virtio_read(h,0)==VIRTIO_MAGIC && chos_virtio_read(h,8)==2);
    assert(chos_virtio_read(h+512,0)==0 && chos_virtio_read(h+1,0)==0);
    chos_virtio_write(h,0x30,1);assert(r16((uintptr_t)bar_mem+26)==0xffff);
    chos_virtio_write(h,0x38,8);assert(chos_virtio_read(h,0x34)==8);
    chos_virtio_write(h,0x44,1);chos_virtio_write(h,0x44,0);assert(chos_virtio_read(h,0x44)==1);
    chos_virtio_write(h,0x80,0x12340000);assert(r32((uintptr_t)bar_mem+32)==0x12340000);
    w16((uintptr_t)bar_mem+30,3);chos_virtio_write(h,0x50,1);
    assert(r16((uintptr_t)bar_mem+268)==1 && r16((uintptr_t)bar_mem+22)==1);
    w32((uintptr_t)bar_mem+768,0x11223344);assert(chos_virtio_read(h,0x100)==0x11223344);
    assert(chos_virtio_read(h,0x140)==0);chos_virtio_write(h,0x70,15);assert(chos_virtio_read(h,0x70)==15);
    assert(chos_virtio_use_pci(&boot,&acpi,&dev,1)==1 && enables==1);
    chos_virtio_write(h,0x50,2);assert(chos_virtio_read(h,0x70)==128);
    reset();cfg[0x68/4]=(cfg[0x68/4]&~0xff00u)|0x4000u;assert(!chos_virtio_use_pci(&boot,&acpi,&dev,1) && !enables);
    reset();cfg[0x4c/4]=4097;assert(!chos_virtio_use_pci(&boot,&acpi,&dev,1));
    reset();cfg[0x80/4]=769;assert(!chos_virtio_use_pci(&boot,&acpi,&dev,1));
    reset();acpi.dma_translation_unverified=1;assert(!chos_virtio_use_pci(&boot,&acpi,&dev,1) && !mappings);acpi.dma_translation_unverified=0;
    reset();assert(chos_virtio_use_pci(&boot,&acpi,&dev,1)==1);h=aperture;
    w16((uintptr_t)bar_mem+30,16);chos_virtio_write(h,0x50,0);assert(chos_virtio_read(h,0x70)==128);
    reset();uintptr_t first=chos_virtio_pci_admit(&boot,&acpi,&dev);assert(first);
    chos_pci_device_t second=dev;second.device=1;
    assert(chos_virtio_pci_admit(&boot,&acpi,&second)==first+512 && chos_virtio_pci_count()==2);
    chos_virtio_seal_bindings();assert(chos_virtio_pci_admit(&boot,&acpi,&dev)==first);
    second.device=2;assert(!chos_virtio_pci_admit(&boot,&acpi,&second) && chos_virtio_pci_count()==2);
    reset();chos_virtio_use_mmio((uintptr_t)bar_mem);h=aperture;
    chos_virtio_write(h,0x70,15);assert(r32(h+0x70)==15 && chos_virtio_read(h,0x70)==15);
    puts("VirtIO transport: modern PCI discovery, bounded caps/config/notify, queue vectors, MMIO preservation and DMA topology rejection passed");
}
