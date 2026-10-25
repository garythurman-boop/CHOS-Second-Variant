#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CHOS_E1000E_TEST 1
#define E1000_WAIT_SPINS 128u
#include "../kernel/platform/e1000e.c"
static uint32_t registers[0x6000/4];
static unsigned mode,master_cleared;
static uint64_t counter;
uint64_t timer_counter(void){return counter++;}
uint64_t timer_frequency_hz(void){return 100000;}
int chos_pci_map_bar(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d,unsigned i,uint64_t *base,uint64_t *size){(void)b;(void)a;(void)d;(void)i;*base=0x100000;*size=0x20000;return mode!=1;}
int chos_pci_read_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t s,uint8_t bus,uint8_t d,uint8_t f,uint16_t off,uint32_t *v){(void)b;(void)a;(void)s;(void)bus;(void)d;(void)f;(void)off;*v=6;return 1;}
int chos_pci_write_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t s,uint8_t bus,uint8_t d,uint8_t f,uint16_t off,uint32_t v){(void)b;(void)a;(void)s;(void)bus;(void)d;(void)f;(void)off;if(!(v&4))master_cleared++;return 1;}
int chos_pci_enable_memory_busmaster(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d){(void)b;(void)a;(void)d;return 1;}
uint32_t e_test_read(unsigned off){return registers[off/4];}
void e_test_write(unsigned off,uint32_t value){
 registers[off/4]=value;
 if(off==CTRL && mode!=2)registers[off/4]&=~RESET;
 if(off==0x3818 && e_running && mode!=3){unsigned i=(value+E_RING-1)%E_RING;e_tx[i][3]=mode==4?3:1;}
}
static chos_boot_info_t boot;
static chos_acpi_info_t acpi={.valid=1};
static chos_pci_device_t dev={.vendor_id=0x8086,.device_id=0x10d3,.class_code=2};
static void reset(void){e_attempted=e_running=0;e_bar=0;e_boot=0;mode=counter=master_cleared=0;zero(registers,sizeof(registers));registers[STATUS/4]=2;registers[0x5400/4]=0x12345602;registers[0x5404/4]=0x9a78;acpi.dma_translation_unverified=0;}
static void start(void){assert(chos_e1000e_init(&boot,&acpi,&dev));assert(e_running);}
int main(void){
 uint8_t frame[E_BUFFER];size_t size;
 reset();acpi.dma_translation_unverified=1;assert(!chos_e1000e_init(&boot,&acpi,&dev)&&!e_attempted);
 reset();mode=1;assert(!chos_e1000e_init(&boot,&acpi,&dev)&&e_attempted);
 reset();mode=2;assert(!chos_e1000e_init(&boot,&acpi,&dev)&&master_cleared);
 reset();registers[0x5400/4]=0;registers[0x5404/4]=0;assert(!chos_e1000e_init(&boot,&acpi,&dev));
 reset();start();memset(frame,0x54,sizeof(frame));
 assert(!chos_e1000e_send(frame,1519));
 for(unsigned n=0;n<3*E_RING;n++){
  assert(chos_e1000e_send(frame,42));unsigned submitted=(e_tx_next+E_RING-1)%E_RING;
  assert(e_tx_buffers[submitted][41]==0x54 && e_tx_buffers[submitted][42]==0 && (e_tx[submitted][2]&0xffff)==60);
  unsigned i=e_rx_next;memset(e_rx_buffers[i],0x54,60);e_rx[i][2]=60;e_rx[i][3]=3;
  assert(chos_e1000e_receive(frame,sizeof(frame),&size)==1&&size==60);
 }
 assert(e_info.tx_frames==96&&e_info.rx_frames==96);
 assert(chos_e1000e_receive(frame,sizeof(frame),&size)==0&&!size);
 unsigned i=e_rx_next;e_rx[i][2]=60;e_rx[i][3]=0x103;
 assert(chos_e1000e_receive(frame,sizeof(frame),&size)==0&&!size&&e_running);
 i=e_rx_next;e_rx[i][2]=E_BUFFER+1;e_rx[i][3]=3;
 assert(chos_e1000e_receive(frame,sizeof(frame),&size)==-1&&!e_running&&master_cleared);
 reset();start();registers[STATUS/4]=0;assert(!chos_e1000e_send(frame,60)&&e_running);
 chos_net_device_info_t info;chos_e1000e_get_info(&info);assert(info.link_status_known&&!info.link_up);
 registers[STATUS/4]=2;assert(chos_e1000e_send(frame,60));
 reset();start();mode=3;assert(!chos_e1000e_send(frame,60)&&!e_running&&master_cleared);
 reset();start();mode=4;assert(!chos_e1000e_send(frame,60)&&!e_running);
 puts("82574L e1000e reset, identity DMA refusal, descriptor wrap, padding, bounds, errors, link and timeout passed");
}
