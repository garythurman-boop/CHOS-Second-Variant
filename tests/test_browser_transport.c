#include <assert.h>
#include <stdio.h>
#include "../kernel/core/browser_kernel_api.h"
static int receive_result,send_result,close_result,valid=1;
static chos_tcp_state_t state=CHOS_TCP_ESTABLISHED;
int chos_tcp_get_info_for_owner(uint32_t owner,int handle,chos_tcp_socket_info_t *out){assert(owner==UINT32_MAX&&handle==1);out->state=state;return valid;}
int chos_tcp_send_for_owner(uint32_t o,int h,const void *b,size_t n){(void)b;(void)n;assert(o==UINT32_MAX&&h==1);return send_result;}
int chos_tcp_recv_for_owner(uint32_t o,int h,void *b,size_t n){(void)b;(void)n;assert(o==UINT32_MAX&&h==1);return receive_result;}
int chos_tcp_close_for_owner(uint32_t o,int h){assert(o==UINT32_MAX&&h==1);return close_result;}
int main(void){
 char data[8];
 assert(chos_net_tcp_recv(1,data,8)==-CHOS_ERRNO_EAGAIN);
 receive_result=3;assert(chos_net_tcp_recv(1,data,8)==3);
 state=CHOS_TCP_CLOSE_WAIT;assert(chos_net_tcp_recv(1,data,8)==3); /* drain before EOF */
 receive_result=0;assert(chos_net_tcp_recv(1,data,8)==0);
 state=CHOS_TCP_FAILED;assert(chos_net_tcp_recv(1,data,8)==-CHOS_ERRNO_EIO);
 valid=0;assert(chos_net_tcp_recv(1,data,8)==-CHOS_ERRNO_EBADF);
 assert(chos_net_tcp_send(1,data,8)==-CHOS_ERRNO_EAGAIN);
 send_result=3;assert(chos_net_tcp_send(1,data,8)==3);
 assert(chos_net_tcp_close(1)==-CHOS_ERRNO_EAGAIN);
 close_result=1;assert(chos_net_tcp_close(1)==0);
 puts("UEFI browser transport: no-data EAGAIN, drain-before-EOF, errors, send/close retry passed");
}
