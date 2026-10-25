#include "chos/syscall.h"
/* Built-in browser adapter for the single-CPU UEFI kernel GUI runtime.
 * Shares the real browser parser/UI/network state machine; never issues SVC. */
#include "chos/gui.h"
#include "chos/net_tcp.h"
#include "chos/net_stack.h"
#include "chos/net_dns_api.h"
#include "chos/storage.h"
#define KERNEL_BROWSER_OWNER UINT32_MAX
static int kernel_browser_active;
static inline void *chos_memcpy(void *destination, const void *source, size_t length) {
    unsigned char *d = (unsigned char *)destination;
    const unsigned char *s = (const unsigned char *)source;
    size_t i;
    for (i = 0u; i < length; ++i) d[i] = s[i];
    return destination;
}

static inline void *chos_memset(void *destination, int value, size_t length) {
    unsigned char *d = (unsigned char *)destination;
    size_t i;
    for (i = 0u; i < length; ++i) d[i] = (unsigned char)value;
    return destination;
}

static inline void *chos_memmove(void *destination, const void *source, size_t length) {
    unsigned char *d = (unsigned char *)destination;
    const unsigned char *s = (const unsigned char *)source;
    size_t i;
    if ((uintptr_t)d <= (uintptr_t)s) {
        for (i = 0u; i < length; ++i) d[i] = s[i];
    } else {
        for (i = length; i > 0u; --i) d[i - 1u] = s[i - 1u];
    }
    return destination;
}
static inline int chos_strncmp(const char *left, const char *right, size_t limit) {
    size_t i;
    if (left == NULL || right == NULL) return left == right ? 0 : (left == NULL ? -1 : 1);
    for (i = 0u; i < limit && left[i] != '\0' && left[i] == right[i]; ++i) { }
    if (i == limit) return 0;
    return (int)(unsigned char)left[i] - (int)(unsigned char)right[i];
}
static inline size_t chos_strlcpy(char *destination, const char *source, size_t capacity) {
    size_t i = 0u;
    if (source == NULL) source = "";
    if (capacity != 0u) {
        while (i + 1u < capacity && source[i] != '\0') { destination[i] = source[i]; ++i; }
        destination[i] = '\0';
    }
    while (source[i] != '\0') ++i;
    return i;
}

static inline size_t chos_strlen(const char *text) {
    size_t length = 0u;
    if (text == NULL) return 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static inline int chos_strcmp(const char *left, const char *right) {
    size_t i = 0u;
    if (left == NULL || right == NULL) return left == right ? 0 : (left == NULL ? -1 : 1);
    while (left[i] != '\0' && left[i] == right[i]) ++i;
    return (int)(unsigned char)left[i] - (int)(unsigned char)right[i];
}


static long chos_gui_submit(chos_gui_message_t *m,size_t n) {return chos_gui_submit_message(KERNEL_BROWSER_OWNER,m,n);}
static long chos_gui_poll_event(chos_gui_message_t *m,size_t n) {(void)n;return chos_gui_pop_event(KERNEL_BROWSER_OWNER,m);}
static void chos_exit(int status) {(void)status;kernel_browser_active=0;chos_tcp_drop_owner(KERNEL_BROWSER_OWNER);chos_gui_process_exited(KERNEL_BROWSER_OWNER);}
static long chos_net_tcp_open(uint16_t p){return chos_tcp_open_for_owner(KERNEL_BROWSER_OWNER,p);}
static long chos_net_tcp_connect(int h,uint32_t ip,uint16_t p){return chos_tcp_connect_for_owner(KERNEL_BROWSER_OWNER,h,ip,p)?0:-1;}
static long chos_net_tcp_send(int h,const void *p,size_t n){
    if(!n)return 0;
    int result=chos_tcp_send_for_owner(KERNEL_BROWSER_OWNER,h,p,n);
    return result<0?-CHOS_ERRNO_EBADF:result==0?-CHOS_ERRNO_EAGAIN:result;
}
static long chos_net_tcp_recv(int h,void *p,size_t n){
    chos_tcp_socket_info_t info;
    if(!chos_tcp_get_info_for_owner(KERNEL_BROWSER_OWNER,h,&info))return -CHOS_ERRNO_EBADF;
    if(!n)return 0;
    int result=chos_tcp_recv_for_owner(KERNEL_BROWSER_OWNER,h,p,n);
    if(result<0)return -CHOS_ERRNO_EBADF;
    if(result)return result;
    if(info.state==CHOS_TCP_CLOSE_WAIT || info.state==CHOS_TCP_FIN_WAIT_2 || info.state==CHOS_TCP_TIME_WAIT || info.state==CHOS_TCP_CLOSED)return 0;
    return info.state==CHOS_TCP_FAILED?-CHOS_ERRNO_EIO:-CHOS_ERRNO_EAGAIN;
}
static long chos_net_tcp_close(int h){
    int result=chos_tcp_close_for_owner(KERNEL_BROWSER_OWNER,h);
    return result<0?-CHOS_ERRNO_EBADF:result==0?-CHOS_ERRNO_EAGAIN:0;
}
static long chos_net_tcp_abort(int h){return chos_tcp_abort_for_owner(KERNEL_BROWSER_OWNER,h);}
static long chos_net_tcp_status(int h,chos_tcp_socket_info_t *p){return chos_tcp_get_info_for_owner(KERNEL_BROWSER_OWNER,h,p)?0:-1;}
static long chos_net_dns_lookup(const char *s){return chos_net_dns_lookup_start(s)?0:-1;}
static long chos_net_dns_status(chos_dns_api_info_t *out){
    chos_net_dns_info_t info;chos_net_dns_get_info(&info);chos_memset(out,0,sizeof(*out));
    out->state=(chos_dns_api_state_t)info.state;chos_strlcpy(out->hostname,info.hostname,sizeof(out->hostname));
    out->ipv4_address=info.ipv4_address;out->ttl_seconds=info.ttl_seconds;out->query_id=info.query_id;out->retries=info.retries;return 0;
}
static char kernel_bookmarks[2048];static size_t kernel_bookmark_size,kernel_bookmark_offset;
static long chos_open(const char *path){kernel_bookmark_offset=0;return storage_read_file(path,kernel_bookmarks,sizeof(kernel_bookmarks),&kernel_bookmark_size)?1:-1;}
static long chos_read(int fd,void *p,size_t n){if(fd!=1)return -1;if(n>kernel_bookmark_size-kernel_bookmark_offset)n=kernel_bookmark_size-kernel_bookmark_offset;chos_memcpy(p,kernel_bookmarks+kernel_bookmark_offset,n);kernel_bookmark_offset+=n;return (long)n;}
static long chos_close(int fd){return fd==1?0:-1;}
static long chos_write_file(const char *path,const void *p,size_t n){return storage_write_file(path,p,n)?(long)n:-1;}
