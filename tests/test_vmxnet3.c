#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CHOS_VMXNET3_TEST 1
#define VMX_WAIT_SPINS 128u
#include "../kernel/platform/vmxnet3.c"
static unsigned model,master_cleared;
static uint32_t last_command,revision=1,upt=1;
uint64_t timer_counter(void){static uint64_t n;return ++n;}
uint64_t timer_frequency_hz(void){return 100000;}
int chos_pci_map_bar(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d,unsigned i,uint64_t *address,uint64_t *size){(void)b;(void)a;(void)d;*address=0x100000+i*0x10000;*size=0x1000;return model!=6;}
int chos_pci_read_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t s,uint8_t bus,uint8_t d,uint8_t f,uint16_t o,uint32_t *v){(void)b;(void)a;(void)s;(void)bus;(void)d;(void)f;(void)o;*v=6;return 1;}
int chos_pci_write_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t s,uint8_t bus,uint8_t d,uint8_t f,uint16_t o,uint32_t v){(void)b;(void)a;(void)s;(void)bus;(void)d;(void)f;(void)o;if(!(v&4))master_cleared++;return 1;}
int chos_pci_enable_memory_busmaster(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d){(void)b;(void)a;(void)d;return 1;}
uint32_t vmx_test_read(unsigned bar,unsigned offset){
 if(bar==0)return 0;
 if(offset==0)return revision;
 if(offset==8)return upt;
 if(offset==0x40)return model==7?1:0;
 if(offset!=0x20)return 0;
 if(last_command==0xf00d0003)return 0x12345602;
 if(last_command==0xf00d0004)return 0x9a78;
 if(last_command==CMD_LINK)return 0x27100001;
 return model==5 && last_command==CMD_ACTIVATE?1:0;
}
void vmx_test_write(unsigned bar,unsigned offset,uint32_t v){
 if(bar==1 && offset==0x20)last_command=v;
 if(bar==1 && offset==0)revision=v;
 if(bar==1 && offset==8)upt=v;
 if(bar==0 && offset==0x600 && model!=2){
  unsigned sent=(v+RING-1)%RING;
  assert((tx_ring[sent][2]&0x3fff)==60);
  tx_complete[tx_comp_index][0]=model==1?RING:sent;
  tx_complete[tx_comp_index][3]=tx_comp_generation<<31;
 }
}
static chos_boot_info_t boot;
static chos_acpi_info_t acpi={.valid=1};
static chos_pci_device_t dev={.vendor_id=0x15ad,.device_id=0x07b0,.class_code=2};
static void reset(void){attempted=vmx_running=0;bars[0]=bars[1]=0;platform_boot=0;model=0;revision=upt=1;master_cleared=0;acpi.dma_translation_unverified=0;}
static void start(void){assert(chos_vmxnet3_init(&boot,&acpi,&dev));assert(vmx_running);}
int main(void){
 uint8_t frame[1518]={0};size_t count;
 reset();acpi.dma_translation_unverified=1;assert(!chos_vmxnet3_init(&boot,&acpi,&dev)&&!attempted);
 reset();model=6;assert(!chos_vmxnet3_init(&boot,&acpi,&dev));assert(attempted&&!vmx_running);assert(!chos_vmxnet3_init(&boot,&acpi,&dev));
 reset();revision=0;assert(!chos_vmxnet3_init(&boot,&acpi,&dev));
 reset();model=5;assert(!chos_vmxnet3_init(&boot,&acpi,&dev)&&master_cleared);
 reset();start();
 assert(!chos_vmxnet3_send(frame,1519));assert(!chos_vmxnet3_send(frame,13));
 for(unsigned n=0;n<3*RING;n++){
  assert(chos_vmxnet3_send(frame,60));
  unsigned i=rx_index;memset(rx_buffers[i],n,60);
  rx_complete[rx_comp_index][0]=i|(3u<<14);
  rx_complete[rx_comp_index][2]=60;
  rx_complete[rx_comp_index][3]=(3u<<24)|(rx_comp_generation<<31);
  assert(chos_vmxnet3_receive(frame,sizeof(frame),&count)==1&&count==60&&frame[59]==(uint8_t)n);
 }
 assert(vmx_info.tx_frames==96&&vmx_info.rx_frames==96);
 assert(chos_vmxnet3_receive(frame,sizeof(frame),&count)==0&&!count);
 rx_complete[rx_comp_index][0]=rx_index|(3u<<14);rx_complete[rx_comp_index][2]=BUFFER+1;
 rx_complete[rx_comp_index][3]=(3u<<24)|(rx_comp_generation<<31);
 assert(chos_vmxnet3_receive(frame,sizeof(frame),&count)==0&&vmx_running&&!count);
 rx_complete[rx_comp_index][0]=RING|(3u<<14);rx_complete[rx_comp_index][3]=(3u<<24)|(rx_comp_generation<<31);
 assert(chos_vmxnet3_receive(frame,sizeof(frame),&count)==-1&&!vmx_running&&master_cleared);
 reset();start();model=1;assert(!chos_vmxnet3_send(frame,60)&&!vmx_running);
 reset();start();model=2;assert(!chos_vmxnet3_send(frame,60)&&!vmx_running);
 reset();start();model=7;assert(chos_vmxnet3_receive(frame,sizeof(frame),&count)==-1&&!vmx_running);
 puts("VMXNET3 negotiation, ring wrap, bounds, completions, timeout and DMA quarantine passed");
}
