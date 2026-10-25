#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../kernel/net/net_device.c"
static int initialized,virtio_available;
int virtio_net_init(void){++initialized;return virtio_available;}
int virtio_net_ready(void){return virtio_available;}
void virtio_net_get_info(chos_virtio_net_info_t *out){memset(out,0,sizeof(*out));out->ready=virtio_available;}
const char *virtio_net_last_error(void){return "virtio unavailable";}
int virtio_net_send_frame(const void *p,size_t n){(void)p;return n?1:0;}
int virtio_net_receive_frame(void *p,size_t n,size_t *got){(void)p;(void)n;*got=0;return 0;}
static int native_ready(void){return 0;}
static const char *native_error(void){return "native queue quarantined";}
int main(void){
 chos_net_device_info_t info;size_t got=99;
 assert(!chos_net_device_bind(NULL));chos_net_device_get_info(&info);assert(!info.ready);
 assert(chos_net_device_receive(NULL,0,&got)==-1&&!got);
 static const chos_net_device_ops_t native={"test native",native_ready,virtio_net_get_info,virtio_net_send_frame,virtio_net_receive_frame,native_error};
 assert(chos_net_device_bind(&native));assert(!chos_net_device_init()&&!initialized);
 assert(!strcmp(chos_net_device_error(),"native queue quarantined"));
 assert(!chos_net_device_bind(&virtio)); /* failed claimed device has no fallback DMA owner */
 selected=NULL;virtio_available=1;assert(chos_net_device_init()&&initialized==1);
 assert(chos_net_device_ready()&&!strcmp(chos_net_device_name(),"VirtIO Ethernet"));
 assert(chos_net_device_send("x",1));assert(!chos_net_device_bind(&native));
 puts("Ethernet backend binding, unsupported state and failed-owner isolation passed");
}
