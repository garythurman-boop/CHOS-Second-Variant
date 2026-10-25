#include "chos/dma.h"
/* Modern VirtIO PCI capability transport and existing MMIO transport.
 * PCI regions are discovered and sized through firmware resources; no assigned
 * BAR, bus mastering, cache or IOMMU assumption is inferred from a PCI ID alone.
 * Device queue drivers retain one implementation for both transports. */
#include "chos/virtio_transport.h"
#define MAX_TRANSPORTS 32u
#define SLOT_STRIDE 0x200u
#define VIRTIO_MAGIC 0x74726976u

typedef struct {
    uintptr_t common,notify,isr,config;
    uint64_t notify_size,config_size;
    uint32_t multiplier,type;
    int failed;
} pci_transport_t;
static struct {pci_transport_t value;uint8_t padding[SLOT_STRIDE-sizeof(pci_transport_t)];}
    transports[MAX_TRANSPORTS] __attribute__((aligned(SLOT_STRIDE)));
static uintptr_t aperture;
static int pci_mode;
static unsigned transport_count;
static int bindings_sealed;
static chos_pci_device_t identities[MAX_TRANSPORTS];
static const char *last_error="VirtIO transport not initialized";
static void barrier(void) {
#ifdef __aarch64__
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}
static uint32_t r32(uintptr_t a){return *(volatile uint32_t *)a;}
static uint16_t r16(uintptr_t a){return *(volatile uint16_t *)a;}
static uint8_t r8(uintptr_t a){return *(volatile uint8_t *)a;}
static void w32(uintptr_t a,uint32_t v){*(volatile uint32_t *)a=v;barrier();}
static void w16(uintptr_t a,uint16_t v){*(volatile uint16_t *)a=v;barrier();}
static void w8(uintptr_t a,uint8_t v){*(volatile uint8_t *)a=v;barrier();}
static void zero(void *p,size_t n){uint8_t *b=p;for(size_t i=0;i<n;++i)b[i]=0;}
void chos_virtio_use_mmio(uintptr_t base) {
    if(aperture)return; /* never replace live DMA transport bindings */
    aperture=base;pci_mode=0;last_error=base?"validated VirtIO MMIO":"VirtIO MMIO absent";
}
uintptr_t chos_virtio_aperture(void){return aperture;}
const char *chos_virtio_transport_error(void){return last_error;}
static pci_transport_t *lookup(uintptr_t handle) {
    uintptr_t start=(uintptr_t)transports;
    if(!pci_mode || handle<start || handle-start>=sizeof(transports) || (handle-start)%SLOT_STRIDE)return 0;
    pci_transport_t *t=&transports[(handle-start)/SLOT_STRIDE].value;
    return t->type?t:0;
}
static uint32_t device_type(uint16_t id) {
    if(id>=0x1040 && id<0x1080)return id-0x1040;
    switch(id){case 0x1000:return 1;case 0x1001:return 2;case 0x1005:return 4;default:return 0;}
}
static int config_read(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d,unsigned off,uint32_t *v) {
    return chos_pci_read_config32(b,a,d->segment,d->bus,d->device,d->function,(uint16_t)off,v);
}
static int discover(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d,pci_transport_t *out) {
    uint64_t bases[6]={0},sizes[6]={0};uint8_t mapped[6]={0},visited[64]={0};
    uint32_t next;unsigned capabilities=0;
    if(!config_read(b,a,d,0x34,&next))return 0;
    next&=255;
    while(next) {
        if(next<0x40 || next>0xfc || (next&3) || visited[next/4] || ++capabilities>48)return 0;
        visited[next/4]=1;
        uint32_t head,bar_word,offset,length;
        if(!config_read(b,a,d,next,&head))return 0;
        unsigned following=(head>>8)&255;
        if((head&255)==9) {
            unsigned cap_length=(head>>16)&255,type=head>>24;
            if(cap_length<16 || cap_length>256-next)return 0;
            if(type>=1 && type<=4) {
                if(!config_read(b,a,d,next+4,&bar_word) || !config_read(b,a,d,next+8,&offset) || !config_read(b,a,d,next+12,&length))return 0;
                unsigned bar=bar_word&255;
                if(bar>=6 || !length)return 0;
                if(!mapped[bar]) {
                    if(!chos_pci_map_bar(b,a,d,bar,&bases[bar],&sizes[bar]))return 0;
                    mapped[bar]=1;
                }
                if(offset>sizes[bar] || length>sizes[bar]-offset || bases[bar]>UINTPTR_MAX-offset)return 0;
                uintptr_t region=(uintptr_t)(bases[bar]+offset);
                if(type==1) {
                    if(out->common || length<56 || (region&3))return 0;
                    out->common=region;
                } else if(type==2) {
                    if(out->notify || cap_length<20 || length<2 || (region&1) || !config_read(b,a,d,next+16,&out->multiplier))return 0;
                    out->notify=region;out->notify_size=length;
                } else if(type==3) {if(out->isr)return 0;out->isr=region;}
                else {if(out->config || (region&3))return 0;out->config=region;out->config_size=length;}
            }
        }
        next=following;
    }
    return out->common && out->notify;
}
unsigned chos_virtio_pci_count(void){return transport_count;}
void chos_virtio_seal_bindings(void){bindings_sealed=1;}
uintptr_t chos_virtio_pci_admit(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,const chos_pci_device_t *d) {
    if(!boot || !acpi || !acpi->valid || acpi->dma_translation_unverified || !d) {
        last_error="VirtIO PCI firmware/DMA topology unavailable";return 0;
    }
    if(aperture && !pci_mode){last_error="mixed MMIO/PCI VirtIO transports unsupported";return 0;}
    for(unsigned i=0;i<transport_count;++i) {
        const chos_pci_device_t *p=&identities[i];
        if(p->segment==d->segment && p->bus==d->bus && p->device==d->device && p->function==d->function)
            return (uintptr_t)&transports[i];
    }
    if(!aperture && !transport_count)bindings_sealed=0;
    if(bindings_sealed || transport_count>=MAX_TRANSPORTS){last_error="VirtIO bindings sealed or capacity reached";return 0;}
#ifdef __aarch64__
 if(!chos_dma_identity_ready()){last_error="VirtIO PCI currently requires verified identity DMA with data cache off";return 0;}
#endif
    uint32_t type=device_type(d->device_id);
    if(d->vendor_id!=0x1af4 || !type || (d->header_type&127)){last_error="not a supported VirtIO PCI identity";return 0;}
    pci_transport_t item={0};
    if(!discover(boot,acpi,d,&item)){last_error="malformed/unmapped VirtIO PCI capabilities";return 0;}
    if(!chos_pci_enable_memory_busmaster(boot,acpi,d)){last_error="VirtIO PCI memory/bus-master enable failed";return 0;}
    uint32_t command;
    if(!config_read(boot,acpi,d,4,&command) || !chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,4,(command&0xffffu)|0x400u)) {
        last_error="VirtIO PCI INTx disable failed";return 0;
    }
    if(!transport_count)zero(transports,sizeof(transports));
    w16(item.common+16,0xffff);item.type=type;
    identities[transport_count]=*d;transports[transport_count].value=item;
    uintptr_t handle=(uintptr_t)&transports[transport_count++];
    pci_mode=1;aperture=(uintptr_t)transports;last_error="modern VirtIO PCI transport admitted";return handle;
}
unsigned chos_virtio_use_pci(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,const chos_pci_device_t *devices,unsigned count) {
    if(aperture)return transport_count;
    if(!devices || count>CHOS_PCI_MAX_DEVICES){last_error="invalid VirtIO PCI inventory";return 0;}
    for(unsigned i=0;i<count;++i)(void)chos_virtio_pci_admit(boot,acpi,&devices[i]);
    chos_virtio_seal_bindings();return transport_count;
}
static uint64_t irq_save(void) {
#ifdef __aarch64__
    uint64_t v;__asm__ volatile("mrs %0,daif\n\tmsr daifset,#2":"=r"(v)::"memory");return v;
#else
    return 0;
#endif
}
static void irq_restore(uint64_t v) {
#ifdef __aarch64__
    __asm__ volatile("msr daif,%0"::"r"(v):"memory");
#else
    (void)v;
#endif
}
uint32_t chos_virtio_read(uintptr_t handle,uint32_t reg) {
    if(!aperture || reg>=SLOT_STRIDE || (reg&3))return 0;
    if(!pci_mode) {
        if(handle<aperture || handle-aperture>=MAX_TRANSPORTS*SLOT_STRIDE || (handle-aperture)%SLOT_STRIDE)return 0;
        return r32(handle+reg);
    }
    pci_transport_t *t=lookup(handle);if(!t)return 0;
    if(reg==0)return VIRTIO_MAGIC;
    if(reg==4)return 2;
    if(reg==8)return t->type;
    if(reg==12)return 0x1af4;
    if(reg==0x70)return t->failed?128:r8(t->common+20);
    if(t->failed)return 0;
    switch(reg) {
    case 0x10:return r32(t->common+4);
    case 0x30:return r16(t->common+22);
    case 0x34:case 0x38:return r16(t->common+24);
    case 0x44:return r16(t->common+28);
    case 0x60:return t->isr?r8(t->isr):0;
    case 0xfc:return r8(t->common+21);
    default:
        if(reg>=0x100 && t->config && reg-0x100<=t->config_size && 4<=t->config_size-(reg-0x100))return r32(t->config+reg-0x100);
        return 0;
    }
}
void chos_virtio_write(uintptr_t handle,uint32_t reg,uint32_t value) {
    if(!aperture || reg>=SLOT_STRIDE || (reg&3))return;
    if(!pci_mode) {
        if(handle>=aperture && handle-aperture<MAX_TRANSPORTS*SLOT_STRIDE && !((handle-aperture)%SLOT_STRIDE))w32(handle+reg,value);
        return;
    }
    pci_transport_t *t=lookup(handle);if(!t || t->failed)return;
    switch(reg) {
    case 0x14:w32(t->common,value);break;
    case 0x24:w32(t->common+8,value);break;
    case 0x20:w32(t->common+12,value);break;
    case 0x30:
        if(value>=r16(t->common+18)){t->failed=1;last_error="VirtIO PCI queue selection out of range";break;}
        w16(t->common+22,(uint16_t)value);w16(t->common+26,0xffff);break;
    case 0x38:w16(t->common+24,(uint16_t)value);break;
    case 0x44:if(value)w16(t->common+28,(uint16_t)value);break;
    case 0x70:
        w8(t->common+20,(uint8_t)value);
        if(!value) {
            unsigned polls;for(polls=0;polls<100000 && r8(t->common+20);++polls)barrier();
            if(polls==100000){t->failed=1;last_error="VirtIO PCI reset timeout";}
        }
        break;
    case 0x50: {
        uint64_t flags=irq_save();
        if(value>=r16(t->common+18)){t->failed=1;last_error="VirtIO PCI notify queue out of range";irq_restore(flags);break;}
        uint16_t saved=r16(t->common+22);w16(t->common+22,(uint16_t)value);
        uint64_t off=(uint64_t)r16(t->common+30)*t->multiplier;
        w16(t->common+22,saved);
        if((off&1) || off>t->notify_size || 2>t->notify_size-off){t->failed=1;last_error="VirtIO PCI notify offset out of range";}
        else w16(t->notify+(uintptr_t)off,(uint16_t)value);
        irq_restore(flags);break;
    }
    case 0x64:break; /* PCI ISR is read-to-clear. */
    default:
        if(reg>=0x80 && reg<=0xa4) {
            unsigned target=reg==0x80?32:reg==0x84?36:reg==0x90?40:reg==0x94?44:reg==0xa0?48:reg==0xa4?52:0;
            if(target)w32(t->common+target,value);
        }
        break;
    }
}
