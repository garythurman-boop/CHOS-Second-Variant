#define CHOS_HOST_TEST 1
#define NVME_WAIT_SPINS 64u
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/arch/aarch64/nvme.c"
static uint8_t pages[7][4096] __attribute__((aligned(4096)));
static uint8_t disk[64*512];
static unsigned allocated,commands,flushes;
static int absent,allocation_failure,stuck_ready,lost,error,bad_cid,bad_queue,fatal;
static uint32_t cc;
uint64_t timer_counter(void) {static uint64_t counter;return counter++;}
uint64_t timer_frequency_hz(void) {return 1000;}
int chos_boot_info_validate(const chos_boot_info_t *boot) {return boot!=NULL;}
int chos_pci_enumerate(const chos_boot_info_t *b,const chos_acpi_info_t *a,chos_pci_device_t *d,size_t capacity,chos_pci_scan_summary_t *s) {
    (void)b;(void)a;assert(capacity);*s=(chos_pci_scan_summary_t){.devices_stored=absent?0:1};
    *d=(chos_pci_device_t){.class_code=1,.subclass=8,.programming_interface=2};return 1;
}
int chos_pci_map_bar(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d,unsigned i,uint64_t *base,uint64_t *size) {
    (void)b;(void)a;(void)d;assert(i==0);*base=0x10000000;*size=0x4000;return 1;
}
int chos_pci_enable_memory_busmaster(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *d) {(void)b;(void)a;(void)d;return 1;}
int pmm_alloc_pages(size_t count,uint64_t *out) {
    assert(count==1);if(allocation_failure || allocated==7)return 0;
    *out=(uintptr_t)pages[allocated++];return 1;
}
uint32_t chos_nvme_mock_read(uint32_t off) {
    if(off==REG_CAP)return 15;
    if(off==REG_CAP+4)return 1u<<5; /* NVM CSS */
    if(off==REG_CC)return cc;
    if(off==REG_CSTS)return fatal?NVME_CSTS_CFS:((cc&1) && !stuck_ready?1:0);
    return 0;
}
void chos_nvme_mock_write(uint32_t off,uint32_t value) {
    if(off==REG_CC){cc=value;return;}
    if(off!=0x1000 && off!=0x1008)return;
    ++commands;if(lost)return;
    int io=off==0x1008;
    volatile nvme_command_t *sq=io?nvme.io_sq:nvme.admin_sq;
    volatile nvme_completion_t *cq=io?nvme.io_cq:nvme.admin_cq;
    unsigned head=io?nvme.io_cq_head:nvme.admin_cq_head;
    unsigned phase=io?nvme.io_phase:nvme.admin_phase;
    nvme_command_t cmd=sq[(value+nvme.queue_depth-1)%nvme.queue_depth];
    uint8_t *data=(void *)(uintptr_t)cmd.prp1;unsigned op=cmd.dword0&255;
    if(!error) {
        if(!io && op==NVME_OP_IDENTIFY) {
            memset(data,0,4096);
            if(cmd.cdw10==1){data[516]=1;}
            else {data[0]=64;data[130]=9;}
        } else if(io && op==NVME_OP_FLUSH)++flushes;
        else if(io) {
            uint64_t lba=(uint64_t)cmd.cdw10|((uint64_t)cmd.cdw11<<32);
            size_t len=((cmd.cdw12&65535)+1)*512;
            assert(lba*512+len<=sizeof(disk));
            if(op==NVME_OP_READ)memcpy(data,disk+lba*512,len);
            else {assert(op==NVME_OP_WRITE);memcpy(disk+lba*512,data,len);}
        }
    }
    cq[head]=(nvme_completion_t){.sq_head=(uint16_t)value,.sq_id=(uint16_t)(bad_queue?7:io),
        .command_id=(uint16_t)((cmd.dword0>>16)+(bad_cid?1:0)),.status=(uint16_t)(phase|(error?2:0))};
}
static void reset(void) {
    memset(&nvme,0,sizeof(nvme));memset(&published_device,0,sizeof(published_device));
    probe_attempted=0;allocated=commands=flushes=cc=0;
    absent=allocation_failure=stuck_ready=lost=error=bad_cid=bad_queue=fatal=0;
    memset(pages,0,sizeof(pages));memset(disk,0,sizeof(disk));
}
static chos_block_device_t online(void) {
    chos_boot_info_t b={0};chos_acpi_info_t a={.valid=1};chos_block_device_t dev;
    assert(chos_nvme_init(&b,&a,&dev));assert(dev.ready && allocated==7);return dev;
}
int main(void) {
    uint8_t input[12*512],output[12*512];chos_block_device_t dev,again;
    chos_boot_info_t b={0};chos_acpi_info_t a={.valid=1};
    reset();dev=online();assert(chos_nvme_init(&b,&a,&again) && allocated==7 && again.context==dev.context);
    for(unsigned i=0;i<sizeof(input);++i)input[i]=(uint8_t)(i*37);
    for(unsigned i=0;i<40;++i) {
        assert(dev.write(&dev,2,12,input));assert(dev.flush(&dev));
        assert(dev.read(&dev,2,12,output) && !memcmp(input,output,sizeof(input)));
    }
    assert(flushes==40);unsigned before=commands;
    assert(!dev.read(&dev,63,2,output) && !dev.write(&dev,UINT64_MAX,1,input) && commands==before);
    error=1;assert(!dev.flush(&dev) && !dev.ready && !chos_nvme_ready());before=allocated;
    assert(!chos_nvme_init(&b,&a,&again) && allocated==before);
    reset();dev=online();lost=1;assert(!dev.read(&dev,0,1,output) && !dev.ready);assert(strstr(chos_nvme_last_error(),"timeout"));
    reset();dev=online();bad_cid=1;assert(!dev.write(&dev,0,1,input) && !dev.ready);
    reset();dev=online();bad_queue=1;assert(!dev.flush(&dev) && !dev.ready);
    reset();dev=online();fatal=1;assert(!dev.read(&dev,0,1,output) && !dev.ready);
    reset();absent=1;assert(!chos_nvme_init(&b,&a,&dev) && !allocated);
    reset();allocation_failure=1;assert(!chos_nvme_init(&b,&a,&dev) && !allocated);
    reset();stuck_ready=1;assert(!chos_nvme_init(&b,&a,&dev));before=allocated;
    assert(!chos_nvme_init(&b,&a,&dev) && allocated==before);
    reset();a.dma_translation_unverified=1;assert(!chos_nvme_init(&b,&a,&dev) && !allocated);
    puts("NVMe mock: chunked I/O, flush, ring wraps, idempotence, bounds, allocation/reset/command failures and permanent quarantine passed");
}
