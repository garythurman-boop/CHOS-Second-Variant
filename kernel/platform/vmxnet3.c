/* Original CHOS polled VMXNET3 revision-1 driver. Wire register/descriptor ABI:
 * VMware VMXNET3/UPT specification; independently encoded, no Linux/QEMU code.
 * Single queue, standard Ethernet, no offloads. Identity DMA only. */
#include <stdint.h>
#include <stddef.h>
#include "chos/vmxnet3.h"
#include "chos/timer.h"
#define RING 32u
#define BUFFER 2048u
#define GEN (1u<<31)
#define CMD_ACTIVATE 0xcafe0000u
#define CMD_QUIESCE 0xcafe0001u
#define CMD_RESET 0xcafe0002u
#define CMD_LINK 0xf00d0002u
#ifndef VMX_WAIT_SPINS
#define VMX_WAIT_SPINS 2000000u
#endif
static const chos_boot_info_t *platform_boot;
static const chos_acpi_info_t *platform_acpi;
static chos_pci_device_t vmx_pci_function;
static uintptr_t bars[2];
static int attempted,vmx_running;
static const char *vmx_error="VMXNET3 not present";
/* Dedicated permanent device-visible pages, never freed on uncertain DMA. */
static uint8_t shared[4096] __attribute__((aligned(4096)));
static uint8_t queues[512] __attribute__((aligned(128)));
static uint8_t tx_data[RING][128] __attribute__((aligned(128)));
static uint32_t tx_ring[RING][4] __attribute__((aligned(512)));
static volatile uint32_t tx_complete[RING][4] __attribute__((aligned(512)));
static uint32_t rx_ring[RING][4] __attribute__((aligned(512)));
static uint32_t rx_body[RING][4] __attribute__((aligned(512)));
static volatile uint32_t rx_complete[RING][4] __attribute__((aligned(512)));
static uint8_t tx_buffer[BUFFER] __attribute__((aligned(128)));
static uint8_t rx_buffers[RING][BUFFER] __attribute__((aligned(128)));
static uint8_t body_buffers[RING][BUFFER] __attribute__((aligned(128)));
static unsigned tx_index,tx_generation=1,tx_comp_index,tx_comp_generation=1;
static unsigned rx_index,rx_generation=1,rx_comp_index,rx_comp_generation=1;
static chos_net_device_info_t vmx_info;
static void barrier(void){
#ifdef __aarch64__
 __asm__ volatile("dsb sy" ::: "memory");
#else
 __asm__ volatile("" ::: "memory");
#endif
}
static uint32_t reg_read(unsigned bar,unsigned off){
#ifdef CHOS_VMXNET3_TEST
 extern uint32_t vmx_test_read(unsigned,unsigned);return vmx_test_read(bar,off);
#else
 return *(volatile uint32_t *)(bars[bar]+off);
#endif
}
static void reg_write(unsigned bar,unsigned off,uint32_t value){
#ifdef CHOS_VMXNET3_TEST
 extern void vmx_test_write(unsigned,unsigned,uint32_t);vmx_test_write(bar,off,value);
#else
 *(volatile uint32_t *)(bars[bar]+off)=value;
#endif
 barrier();
}
static uint32_t command(uint32_t value){reg_write(1,0x20,value);return reg_read(1,0x20);}
static void zero(void *p,size_t n){uint8_t *b=p;while(n--)*b++=0;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static int fail(const char *message){
 vmx_error=message;vmx_running=0;vmx_info.ready=0;
 if(bars[0])reg_write(0,0,1);
 if(platform_boot){uint32_t c;if(chos_pci_read_config32(platform_boot,platform_acpi,vmx_pci_function.segment,vmx_pci_function.bus,vmx_pci_function.device,vmx_pci_function.function,4,&c))
  (void)chos_pci_write_config32(platform_boot,platform_acpi,vmx_pci_function.segment,vmx_pci_function.bus,vmx_pci_function.device,vmx_pci_function.function,4,(c&0xffffu)&~4u);}
 return 0;
}
const char *chos_vmxnet3_error(void){return vmx_error;}
int chos_vmxnet3_ready(void){return vmx_running;}
void chos_vmxnet3_stop(void){if(vmx_running)(void)command(CMD_QUIESCE);(void)fail("VMXNET3 stopped; reboot before rebinding");}
static void descriptor(uint32_t *d,const void *buffer,uint32_t control){
 uintptr_t address=(uintptr_t)buffer;d[0]=(uint32_t)address;d[1]=(uint32_t)((uint64_t)address>>32);d[3]=0;d[2]=control;
}
int chos_vmxnet3_init(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,const chos_pci_device_t *device){
 if(vmx_running)return 1;
 if(attempted){vmx_error="VMXNET3 quarantined until reboot";return 0;}
 if(!boot||!acpi||!acpi->valid||!device||device->vendor_id!=0x15ad||device->device_id!=0x07b0||device->class_code!=2){vmx_error="Not a VMXNET3 Ethernet device";return 0;}
 if(acpi->dma_translation_unverified){vmx_error="VMXNET3 IOMMU topology unsupported";return 0;}
#ifdef __aarch64__
 uint64_t sctlr;__asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));
 if(sctlr&5u){vmx_error="VMXNET3 requires identity DMA with MMU/cache off";return 0;}
#endif
 attempted=1;platform_boot=boot;platform_acpi=acpi;vmx_pci_function=*device;
 uint64_t address[2],size[2];
 if(!chos_pci_map_bar(boot,acpi,device,0,&address[0],&size[0])||!chos_pci_map_bar(boot,acpi,device,1,&address[1],&size[1])||
    size[0]<0x1000||size[1]<0x1000||(address[0]&7)||(address[1]&7)||
    (address[0]<address[1]+size[1]&&address[1]<address[0]+size[0]))return fail("VMXNET3 BARs unavailable or overlapping");
 bars[0]=(uintptr_t)address[0];bars[1]=(uintptr_t)address[1];
 uint32_t pci_command;
 if(!chos_pci_read_config32(boot,acpi,device->segment,device->bus,device->device,device->function,4,&pci_command)||
    !chos_pci_write_config32(boot,acpi,device->segment,device->bus,device->device,device->function,4,((pci_command&0xffffu)|0x402u)&~4u))return fail("VMXNET3 PCI memory enable failed");
 reg_write(0,0,1);(void)command(CMD_QUIESCE);(void)command(CMD_RESET);
 if(!(reg_read(1,0)&1u)||!(reg_read(1,8)&1u))return fail("VMXNET3 revision 1 / UPT 1 unavailable");
 reg_write(1,0,1);reg_write(1,8,1);
 if(reg_read(1,0)!=1 || reg_read(1,8)!=1)return fail("VMXNET3 revision negotiation failed");
 zero(shared,sizeof(shared));zero(queues,sizeof(queues));zero(tx_ring,sizeof(tx_ring));zero((void *)tx_complete,sizeof(tx_complete));
 zero(rx_ring,sizeof(rx_ring));zero(rx_body,sizeof(rx_body));zero((void *)rx_complete,sizeof(rx_complete));zero(&vmx_info,sizeof(vmx_info));
 tx_index=tx_comp_index=rx_index=rx_comp_index=0;tx_generation=tx_comp_generation=rx_generation=rx_comp_generation=1;
 uint32_t low=command(0xf00d0003u),high=command(0xf00d0004u);
 for(unsigned i=0;i<6;i++)vmx_info.mac[i]=(uint8_t)(i<4?low>>(8*i):high>>(8*(i-4)));
 unsigned any=0;for(unsigned i=0;i<6;i++)any|=vmx_info.mac[i];
 if(!any||(vmx_info.mac[0]&1))return fail("VMXNET3 invalid MAC address");
 reg_write(1,0x28,low);reg_write(1,0x30,high);
 /* Shared: header8 + Misc72 + Interrupt40 + Filter528 + configs48 + events24. */
 put32(shared,0xbabefee1u);put32(shared+8,0x01000000u);put32(shared+12,2); /* 64-bit guest, other OS */
 put32(shared+16,1);put32(shared+20,1);put64(shared+40,(uintptr_t)queues);put32(shared+52,sizeof(queues));
 put32(shared+56,1500);shared[60]=1;shared[62]=1;shared[63]=1;
 shared[81]=1;put32(shared+108,1); /* single interrupt, all masked */
 put32(shared+120,5); /* receive own unicast and broadcast; no offloads */
 put32(queues+4,1);put64(queues+16,(uintptr_t)tx_ring);put64(queues+24,(uintptr_t)tx_data);put64(queues+32,(uintptr_t)tx_complete);
 put32(queues+56,RING);put32(queues+60,RING);put32(queues+64,RING);
 uint8_t *rx=queues+256;
 put64(rx+16,(uintptr_t)rx_ring);put64(rx+24,(uintptr_t)rx_body);put64(rx+32,(uintptr_t)rx_complete);
 put32(rx+56,RING);put32(rx+60,RING);put32(rx+64,RING);
 for(unsigned i=0;i<RING;i++){
  descriptor(rx_ring[i],rx_buffers[i],BUFFER|GEN);
  descriptor(rx_body[i],body_buffers[i],BUFFER|(1u<<14)|GEN);
 }
 barrier();
 if(!chos_pci_enable_memory_busmaster(boot,acpi,device))return fail("VMXNET3 bus mastering refused");
 reg_write(1,0x10,(uint32_t)(uintptr_t)shared);reg_write(1,0x18,(uint32_t)((uint64_t)(uintptr_t)shared>>32));
 if(command(CMD_ACTIVATE)!=0)return fail("VMXNET3 activation failed");
 reg_write(0,0,1);reg_write(0,0x800,RING-1);reg_write(0,0xa00,RING-1);
 vmx_running=1;vmx_info.ready=1;vmx_info.mmio_base=bars[0];vmx_info.rx_queue_size=RING;vmx_info.rx_queue_ready=1;
 vmx_error="VMXNET3 revision 1, polled Ethernet ready";return 1;
}
void chos_vmxnet3_get_info(chos_net_device_info_t *out){
 if(!out)return;
 if(vmx_running){uint32_t link=command(CMD_LINK);vmx_info.link_status_known=1;vmx_info.link_up=(link&1u)!=0;}
 *out=vmx_info;
}
int chos_vmxnet3_send(const void *frame,size_t length){
 if(!vmx_running||!frame||length<14||length>CHOS_NET_MAX_ETHERNET_FRAME)return 0;
 for(size_t i=0;i<length;i++)tx_buffer[i]=((const uint8_t *)frame)[i];
 uint32_t *d=tx_ring[tx_index];descriptor(d,tx_buffer,(uint32_t)length|((tx_generation^1u)<<14));d[3]=(1u<<12)|(1u<<13);
 barrier();d[2]=(uint32_t)length|(tx_generation<<14);barrier();
 unsigned submitted=tx_index;if(++tx_index==RING){tx_index=0;tx_generation^=1;}
 reg_write(0,0x600,tx_index);
 uint64_t start=timer_counter(),limit=timer_frequency_hz()/10u;
 for(unsigned spin=0;spin<VMX_WAIT_SPINS;spin++){
  if((tx_complete[tx_comp_index][3]>>31)==tx_comp_generation){
   barrier();uint32_t index=tx_complete[tx_comp_index][0]&4095u;
   if(index!=submitted || ((tx_complete[tx_comp_index][3]>>24)&127u)!=0){++vmx_info.tx_errors;return fail("VMXNET3 invalid TX completion");}
   if(++tx_comp_index==RING){tx_comp_index=0;tx_comp_generation^=1;}
   ++vmx_info.tx_frames;return 1;
  }
  if(limit && timer_counter()-start>=limit)break;
 }
 ++vmx_info.tx_errors;return fail("VMXNET3 TX timeout; DMA quarantined until reboot");
}
int chos_vmxnet3_receive(void *frame,size_t capacity,size_t *length){
 if(length)*length=0;
 if(!vmx_running||!frame||!length)return -1;
 if(reg_read(1,0x40)&3u){(void)fail("VMXNET3 device queue error");return -1;}
 volatile uint32_t *c=rx_complete[rx_comp_index];
 if((c[3]>>31)!=rx_comp_generation)return 0;
 barrier();uint32_t flags=c[0],bytes=c[2]&0x3fffu;
 /* QEMU v10 emits type 0 on its RX completion ring. Treat it as the
  * revision-1 layout only; phase/index/queue/SOP/EOP/buffer bounds still apply. */
 unsigned index=flags&4095u,queue=(flags>>16)&1023u;
 if(index!=rx_index||queue!=0||((flags>>14)&3u)!=3u||(((c[3]>>24)&127u)!=3u && ((c[3]>>24)&127u)!=0u)){++vmx_info.rx_dropped;(void)fail("VMXNET3 invalid/fragmented RX completion");return -1;}
 int valid=bytes>=14&&bytes<=CHOS_NET_MAX_ETHERNET_FRAME&&bytes<=capacity&&!(c[2]&0xc000u);
 if(valid){for(unsigned i=0;i<bytes;i++)((uint8_t *)frame)[i]=rx_buffers[index][i];*length=bytes;++vmx_info.rx_frames;}
 else ++vmx_info.rx_dropped;
 /* Device consumed a head: return only that buffer with the next generation. */
 rx_ring[index][2]=BUFFER|((rx_generation^1u)<<31);barrier();
 if(++rx_index==RING){rx_index=0;rx_generation^=1;}
 if(++rx_comp_index==RING){rx_comp_index=0;rx_comp_generation^=1;}
 reg_write(0,0x800,(rx_index+RING-1)%RING);vmx_info.rx_consumed_idx++;
 return valid?1:0;
}
