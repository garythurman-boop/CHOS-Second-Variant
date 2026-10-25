/* Original CHOS Intel 82574L (8086:10d3) backend. Intel legacy descriptor ABI.
 * One polled queue, software checksums, standard MTU; permanent identity DMA.
 * No PHY/firmware writes intended for arbitrary Intel controllers. */
#include <stdint.h>
#include <stddef.h>
#include "chos/e1000e.h"
#include "chos/timer.h"
#define E_RING 32u
#define E_BUFFER 2048u
#ifndef E1000_WAIT_SPINS
#define E1000_WAIT_SPINS 2000000u
#endif
#define CTRL 0x0000u
#define STATUS 0x0008u
#define CTRL_EXT 0x0018u
#define IMC 0x00d8u
#define RCTL 0x0100u
#define TCTL 0x0400u
#define RESET (1u<<26)
static const chos_boot_info_t *e_boot;
static const chos_acpi_info_t *e_acpi;
static chos_pci_device_t e_device;
static uintptr_t e_bar;
static int e_attempted,e_running;
static const char *e_error="Intel 82574L not present";
/* DMA storage is never freed/rebound after timeout or uncertain completion. */
static uint32_t e_rx[E_RING][4] __attribute__((aligned(128)));
static volatile uint32_t e_tx[E_RING][4] __attribute__((aligned(128)));
static uint8_t e_rx_buffers[E_RING][E_BUFFER] __attribute__((aligned(128)));
static uint8_t e_tx_buffers[E_RING][E_BUFFER] __attribute__((aligned(128)));
static unsigned e_rx_next,e_tx_next;
static chos_net_device_info_t e_info;
static void barrier(void){
#ifdef __aarch64__
 __asm__ volatile("dsb sy" ::: "memory");
#else
 __asm__ volatile("" ::: "memory");
#endif
}
static uint32_t rd(unsigned offset){
#ifdef CHOS_E1000E_TEST
 extern uint32_t e_test_read(unsigned);return e_test_read(offset);
#else
 return *(volatile uint32_t *)(e_bar+offset);
#endif
}
static void wr(unsigned offset,uint32_t value){
#ifdef CHOS_E1000E_TEST
 extern void e_test_write(unsigned,uint32_t);e_test_write(offset,value);
#else
 *(volatile uint32_t *)(e_bar+offset)=value;
#endif
 barrier();
}
static void zero(void *p,size_t n){uint8_t *b=p;while(n--)*b++=0;}
static void address(volatile uint32_t *d,const void *p){uint64_t a=(uintptr_t)p;d[0]=(uint32_t)a;d[1]=(uint32_t)(a>>32);}
static int fail(const char *message){
 e_error=message;e_running=0;e_info.ready=0;
 if(e_bar){wr(IMC,UINT32_MAX);wr(RCTL,0);wr(TCTL,0);}
 if(e_boot){uint32_t c;if(chos_pci_read_config32(e_boot,e_acpi,e_device.segment,e_device.bus,e_device.device,e_device.function,4,&c))
  (void)chos_pci_write_config32(e_boot,e_acpi,e_device.segment,e_device.bus,e_device.device,e_device.function,4,(c&0xffffu)&~4u);}
 return 0;
}
int chos_e1000e_ready(void){return e_running;}
const char *chos_e1000e_error(void){return e_error;}
void chos_e1000e_stop(void){(void)fail("Intel 82574L stopped; reboot before rebinding");}
int chos_e1000e_init(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,const chos_pci_device_t *device){
 if(e_running)return 1;
 if(e_attempted){e_error="Intel 82574L quarantined until reboot";return 0;}
 if(!boot||!acpi||!acpi->valid||!device||device->vendor_id!=0x8086||device->device_id!=0x10d3||device->class_code!=2){e_error="Not an Intel 82574L Ethernet device";return 0;}
 if(acpi->dma_translation_unverified){e_error="Intel 82574L requires verified identity DMA topology";return 0;}
#ifdef __aarch64__
 uint64_t sctlr;__asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));
 if(sctlr&5u){e_error="Intel 82574L requires MMU/cache off identity DMA";return 0;}
#endif
 e_attempted=1;e_boot=boot;e_acpi=acpi;e_device=*device;
 uint64_t base,size;uint32_t command;
 if(!chos_pci_map_bar(boot,acpi,device,0,&base,&size)||size<0x20000u||(base&0x1ffffu))return fail("Intel 82574L BAR0 unavailable or too small");
 e_bar=(uintptr_t)base;
 if(!chos_pci_read_config32(boot,acpi,device->segment,device->bus,device->device,device->function,4,&command)||
    !chos_pci_write_config32(boot,acpi,device->segment,device->bus,device->device,device->function,4,((command&0xffffu)|0x402u)&~4u))return fail("Intel 82574L PCI memory decode unavailable");
 wr(IMC,UINT32_MAX);wr(RCTL,0);wr(TCTL,0);wr(CTRL,rd(CTRL)|RESET);
 uint64_t start=timer_counter(),budget=timer_frequency_hz()/10u;unsigned spin;
 for(spin=0;spin<E1000_WAIT_SPINS;spin++){if(!(rd(CTRL)&RESET))break;if(budget&&timer_counter()-start>=budget)break;}
 if(rd(CTRL)&RESET)return fail("Intel 82574L reset timed out");
 wr(IMC,UINT32_MAX);(void)rd(0xc0); /* clear pending causes; all interrupts stay masked */
 zero(e_rx,sizeof(e_rx));zero((void *)e_tx,sizeof(e_tx));zero(&e_info,sizeof(e_info));
 uint32_t low=rd(0x5400),high=rd(0x5404);unsigned any=0;
 for(unsigned i=0;i<6;i++){e_info.mac[i]=(uint8_t)(i<4?low>>(8*i):high>>(8*(i-4)));any|=e_info.mac[i];}
 if(!any||(e_info.mac[0]&1u))return fail("Intel 82574L EEPROM MAC unavailable");
 wr(0x5400,low);wr(0x5404,(high&0xffffu)|(1u<<31));
 for(unsigned i=0;i<128;i++)wr(0x5200+4*i,0); /* multicast filter, not fictitious Wi-Fi */
 for(unsigned i=0;i<E_RING;i++){address(e_rx[i],e_rx_buffers[i]);address(e_tx[i],e_tx_buffers[i]);}
 e_rx_next=e_tx_next=0;
 wr(CTRL,rd(CTRL)|(1u<<6)|(1u<<5)); /* set link up, auto speed detection */
 wr(CTRL_EXT,rd(CTRL_EXT)|(1u<<28));
 wr(0x5000,0);wr(0x5008,0); /* no offload; legacy RX descriptor format */
 wr(0x2800,(uint32_t)(uintptr_t)e_rx);wr(0x2804,(uint32_t)((uint64_t)(uintptr_t)e_rx>>32));
 wr(0x2808,sizeof(e_rx));wr(0x2810,0);wr(0x2818,E_RING-1);wr(0x2820,0);wr(0x282c,0);wr(0x2828,0x01010000u);
 wr(0x3800,(uint32_t)(uintptr_t)e_tx);wr(0x3804,(uint32_t)((uint64_t)(uintptr_t)e_tx>>32));
 wr(0x3808,sizeof(e_tx));wr(0x3810,0);wr(0x3818,0);wr(0x3828,0x01010000u);
 wr(0x410,0x0060200au);
 if(!chos_pci_enable_memory_busmaster(boot,acpi,device))return fail("Intel 82574L bus mastering refused");
 wr(TCTL,(1u<<1)|(1u<<3)|(15u<<4)|(64u<<12));
 wr(RCTL,(1u<<1)|(1u<<15)|(1u<<26)); /* enable, broadcast, 2048B, strip FCS */
 e_running=1;e_info.ready=1;e_info.mmio_base=e_bar;e_info.rx_queue_size=E_RING;e_info.rx_queue_ready=1;
 e_error="Intel 82574L e1000e polled Ethernet ready";return 1;
}
void chos_e1000e_get_info(chos_net_device_info_t *out){
 if(!out)return;
 if(e_running){uint32_t status=rd(STATUS);if(status==UINT32_MAX){(void)fail("Intel 82574L disappeared");}else{e_info.link_status_known=1;e_info.link_up=(status&2u)!=0;}}
 *out=e_info;
}
int chos_e1000e_send(const void *frame,size_t length){
 if(!e_running||!frame||length<14||length>CHOS_NET_MAX_ETHERNET_FRAME||!(rd(STATUS)&2u))return 0;
 unsigned i=e_tx_next;size_t wire=length<60?60:length;
 for(size_t n=0;n<wire;n++)e_tx_buffers[i][n]=n<length?((const uint8_t *)frame)[n]:0;
 e_tx[i][3]=0;barrier();e_tx[i][2]=(uint32_t)wire|(0x0bu<<24);barrier();
 e_tx_next=(i+1)%E_RING;wr(0x3818,e_tx_next);
 uint64_t start=timer_counter(),budget=timer_frequency_hz()/10u;
 for(unsigned spin=0;spin<E1000_WAIT_SPINS;spin++){
  if(e_tx[i][3]&1u){barrier();if(e_tx[i][3]&0x0eu){++e_info.tx_errors;return fail("Intel 82574L TX error");}++e_info.tx_frames;return 1;}
  if(budget&&timer_counter()-start>=budget)break;
 }
 ++e_info.tx_errors;return fail("Intel 82574L TX timeout; DMA quarantined until reboot");
}
int chos_e1000e_receive(void *frame,size_t capacity,size_t *length){
 if(length)*length=0;
 if(!e_running||!frame||!length)return -1;
 unsigned i=e_rx_next;volatile uint32_t *d=e_rx[i];uint32_t metadata=d[3];
 if(!(metadata&1u))return 0;
 barrier();metadata=d[3];uint32_t bytes=d[2]&0xffffu;
 if(bytes>E_BUFFER || !(metadata&2u)){++e_info.rx_dropped;(void)fail("Intel 82574L malformed/fragmented RX completion");return -1;}
 int valid=bytes>=14 && bytes<=CHOS_NET_MAX_ETHERNET_FRAME && bytes<=capacity && !(metadata&0xff00u);
 if(valid){for(unsigned n=0;n<bytes;n++)((uint8_t *)frame)[n]=e_rx_buffers[i][n];*length=bytes;++e_info.rx_frames;}
 else ++e_info.rx_dropped;
 d[2]=0;d[3]=0;barrier();wr(0x2818,i);e_rx_next=(i+1)%E_RING;++e_info.rx_consumed_idx;
 return valid?1:0;
}
