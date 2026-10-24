#define NET_TX_WAIT_LIMIT 64u
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/arch/aarch64/virtio_net.c"
static uint32_t selected,feature,status;
static unsigned resets;
static int completion_fault;
uintptr_t chos_virtio_aperture(void) {return 0x1000;}
uint32_t chos_virtio_read(uintptr_t h,uint32_t reg) {
    if(h!=0x1000)return 0;
    switch(reg) {
    case REG_MAGIC:return VIRTIO_MAGIC;case REG_VERSION:return 2;case REG_DEVICE_ID:return VIRTIO_DEVICE_NET;
    case REG_DEVICE_FEATURES:return feature?1:VIRTIO_NET_F_MAC|VIRTIO_NET_F_STATUS;
    case REG_QUEUE_NUM_MAX:return 64;case REG_QUEUE_NUM:return queue_size;
    case REG_STATUS:return status;case REG_CONFIG:return 0x12005452;case REG_CONFIG+4:return 0x15634;
    default:return 0;
    }
}
void chos_virtio_write(uintptr_t h,uint32_t reg,uint32_t value) {
    assert(h==0x1000);
    if(reg==REG_STATUS) {status=value;if(!value)++resets;}
    if(reg==REG_DEVICE_FEATURES_SEL)feature=value;
    if(reg==REG_QUEUE_SEL)selected=value;
    if(reg==REG_QUEUE_NOTIFY && value==1 && completion_fault!=1) {
        uint16_t old=tx_used.idx;
        tx_used.ring[old&(queue_size-1u)].id=completion_fault==2?7:0;
        tx_used.idx=(uint16_t)(old+(completion_fault==3?2:1));
    }
}
static void reset(void) {
    mmio_base=0;driver_ready=0;status=selected=feature=resets=0;completion_fault=0;
}
int main(void) {
    uint8_t frame[64]={0},out[128];size_t n=0;
    reset();assert(virtio_net_init());assert(virtio_net_init() && resets==1);
    for(unsigned i=0;i<200;++i)assert(virtio_net_send_frame(frame,sizeof(frame)));
    memcpy(rx_buffers[0]+NET_HEADER_SIZE,frame,sizeof(frame));
    rx_used.ring[0].id=0;
    rx_used.ring[0].len=NET_HEADER_SIZE+sizeof(frame);rx_used.idx=1;
    assert(virtio_net_receive_frame(out,sizeof(out),&n)==1 && n==sizeof(frame) && !memcmp(out,frame,n));
    for(int fault=1;fault<=3;++fault) {
        reset();assert(virtio_net_init());completion_fault=fault;
        assert(!virtio_net_send_frame(frame,sizeof(frame)) && !virtio_net_ready());
        unsigned before=resets;assert(!virtio_net_init() && resets==before);
    }
    reset();assert(virtio_net_init());rx_used.idx=queue_size+1;
    assert(virtio_net_receive_frame(out,sizeof(out),&n)==-1 && !virtio_net_ready());
    reset();assert(virtio_net_init());rx_used.idx=1;rx_used.ring[0].id=queue_size;
    assert(virtio_net_receive_frame(out,sizeof(out),&n)==-1 && !virtio_net_ready());
    puts("VirtIO net mock: TX/RX, queue wraps, repeated init, lost/bad completions, overrun and quarantine passed");
}
