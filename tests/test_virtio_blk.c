#define CHOS_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/arch/aarch64/virtio_blk.c"
static uint32_t regs[128];
static uint8_t disk[32*512];
static int drop_completion,invalid_completion,io_error,reset_stuck;
uintptr_t chos_virtio_aperture(void) {return 0x10000;}
uint32_t chos_blk_mock_read(uintptr_t base,uint32_t o) {
    if(base!=0x10000) return 0;
    if(o==REG_MAGIC)return VIRTIO_MAGIC;
    if(o==REG_VERSION)return 2;
    if(o==REG_DEVICE_ID)return VIRTIO_DEVICE_BLOCK;
    if(o==REG_DEVICE_FEATURES)return regs[REG_DEVICE_FEATURES_SEL/4]?VIRTIO_F_VERSION_1:VIRTIO_BLK_F_FLUSH;
    if(o==REG_QUEUE_NUM_MAX)return 8;
    if(o==REG_CONFIG_CAPACITY_LOW)return 32;
    return regs[o/4];
}
void chos_blk_mock_write(uintptr_t base,uint32_t o,uint32_t v) {
    assert(base==0x10000);regs[o/4]=v;
    if(o==REG_STATUS && !v && reset_stuck) regs[o/4]=1;
    if(o==REG_QUEUE_NOTIFY && !drop_completion) {
        uint32_t bytes=queue_desc[1].len;
        if(request_header.type!=VIRTIO_BLK_T_FLUSH) {
            assert(queue_desc[1].addr==(uintptr_t)dma_bounce);
            assert(bytes<=sizeof(dma_bounce));assert(request_header.sector*512+bytes<=sizeof(disk));
            if(request_header.type==VIRTIO_BLK_T_OUT)memcpy(disk+request_header.sector*512,dma_bounce,bytes);
            else memcpy(dma_bounce,disk+request_header.sector*512,bytes);
        }
        request_status=io_error?1:0;
        queue_used.ring[queue_used.idx&(queue_size-1)].id=invalid_completion?7:0;
        ++queue_used.idx;
    }
}
static void fixture(void) {
    memset(regs,0,sizeof(regs));driver_ready=quarantined=0;
    drop_completion=invalid_completion=io_error=reset_stuck=0;
}
int main(void) {
    chos_block_device_t b;uint8_t source[12*512],out[12*512];
    for(unsigned i=0;i<sizeof(source);++i)source[i]=(uint8_t)i;
    fixture();assert(virtio_blk_init(&b));assert(b.write(&b,3,12,source));assert(b.flush(&b));
    assert(virtio_blk_init(&b));assert(b.read(&b,3,12,out));assert(!memcmp(source,out,sizeof(out)));
    io_error=1;assert(!b.flush(&b));assert(driver_ready);io_error=0;
    invalid_completion=1;assert(!b.read(&b,0,1,out));assert(quarantined && !driver_ready);assert(!virtio_blk_init(&b));
    fixture();assert(virtio_blk_init(&b));drop_completion=1;assert(!b.read(&b,0,1,out));assert(quarantined);assert(!virtio_blk_init(&b));
    fixture();reset_stuck=1;assert(!virtio_blk_init(&b));assert(quarantined);
    puts("VirtIO block: bounded permanent DMA, chunked read/write/flush, repeated init, I/O error, bad/lost completion and reset timeout passed");
}
