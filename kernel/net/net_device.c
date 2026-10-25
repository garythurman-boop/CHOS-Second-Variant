#include "chos/net_device.h"
#include "chos/virtio_net.h"
static const chos_net_device_ops_t *selected;
static const chos_net_device_ops_t virtio={"VirtIO Ethernet",virtio_net_ready,virtio_net_get_info,virtio_net_send_frame,virtio_net_receive_frame,virtio_net_last_error};
int chos_net_device_bind(const chos_net_device_ops_t *ops){
 if(selected)return selected==ops;
 if(!ops||!ops->name||!ops->ready||!ops->get_info||!ops->send||!ops->receive||!ops->error)return 0;
 selected=ops;return 1;
}
int chos_net_device_init(void){
 if(selected)return selected->ready();
 if(virtio_net_init())return chos_net_device_bind(&virtio);
 return 0;
}
int chos_net_device_ready(void){return selected && selected->ready();}
const char *chos_net_device_name(void){return selected?selected->name:"No supported Ethernet adapter";}
const char *chos_net_device_error(void){return selected?selected->error():virtio_net_last_error();}
void chos_net_device_get_info(chos_net_device_info_t *out){
 if(!out)return;
 if(selected)selected->get_info(out);
 else {uint8_t *p=(void *)out;for(size_t i=0;i<sizeof(*out);i++)p[i]=0;}
}
int chos_net_device_send(const void *frame,size_t bytes){return selected?selected->send(frame,bytes):0;}
int chos_net_device_receive(void *frame,size_t capacity,size_t *bytes){if(bytes)*bytes=0;return selected?selected->receive(frame,capacity,bytes):-1;}
