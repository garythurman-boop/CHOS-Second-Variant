#include "chos/thread_api.h"
#include "chos/process_caps.h"
#include "chos/ipc.h"
#include "chos/clock_api.h"
#ifndef CHOS_USER_RUNTIME_H
#define CHOS_USER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include "chos/syscall.h"
#include "chos/net_status_api.h"
#include "chos/net6_api.h"
#include "chos/net_tcp6_api.h"
#include "chos/net_dns6_api.h"
#include "chos/gui_protocol.h"
#include "chos/file_api.h"
#include "chos/net_tcp_api.h"
#include "chos/net_dns_api.h"

/* Tiny freestanding AArch64 user runtime. Include this header when compiling
 * position-independent EL0 code for CHOS. Syscall numbers live in x8, arguments
 * in x0-x2, and results (including negative errno values) return in x0. */
static inline long chos_syscall0(long number) {
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}

static inline long chos_syscall1(long number, long arg0) {
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}

static inline long chos_syscall2(long number, long arg0, long arg1) {
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory", "cc");
    return x0;
}

static inline long chos_syscall3(long number, long arg0, long arg1, long arg2) {
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    register long x2 __asm__("x2") = arg2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory", "cc");
    return x0;
}

static inline long chos_write(const void *buffer, size_t length) {
    return chos_syscall2((long)CHOS_SYS_WRITE, (long)(uintptr_t)buffer, (long)length);
}

static inline long chos_getpid(void) {
    return chos_syscall0((long)CHOS_SYS_GETPID);
}

/* Nonblocking entropy request. Returns bytes copied or -errno; zero means no
 * output requested, -EAGAIN means the platform source has no bytes ready. */
static inline long chos_random_bytes(void *buffer, size_t capacity) {
    return chos_syscall2((long)CHOS_SYS_RANDOM_BYTES, (long)(uintptr_t)buffer, (long)capacity);
}

static inline long chos_yield(void) {
    return chos_syscall0((long)CHOS_SYS_YIELD);
}

static inline long chos_sleep_ticks(uint64_t ticks) {
    return chos_syscall1((long)CHOS_SYS_SLEEP, (long)ticks);
}

static inline long chos_getstatus(uint32_t pid) {
    return chos_syscall1((long)CHOS_SYS_GETSTATUS, (long)pid);
}

static inline long chos_wait(uint32_t pid) {
    return chos_syscall1((long)CHOS_SYS_WAIT, (long)pid);
}

/* Open a CHFS or ROMFS path read-only. Returned descriptors belong to the current process. */
static inline long chos_open(const char *path) {
    return chos_syscall1((long)CHOS_SYS_OPEN, (long)(uintptr_t)path);
}
static inline long chos_read(int fd, void *buffer, size_t length) {
    return chos_syscall3((long)CHOS_SYS_READ, (long)fd, (long)(uintptr_t)buffer, (long)length);
}
static inline long chos_close(int fd) {
    return chos_syscall1((long)CHOS_SYS_CLOSE, (long)fd);
}

/* Enumerate the merged persistent/embedded flat file namespace into caller-owned memory. */
static inline long chos_list_files(chos_file_list_entry_t *entries, size_t capacity) {
    return chos_syscall2((long)CHOS_SYS_LIST_FILES, (long)(uintptr_t)entries, (long)capacity);
}
static inline long chos_list_dir(const char *path, chos_file_list_entry_t *entries, size_t capacity) {
    return chos_syscall3((long)CHOS_SYS_LIST_DIR, (long)(uintptr_t)path,
                         (long)(uintptr_t)entries, (long)capacity);
}
static inline long chos_mkdir(const char *path) {
    return chos_syscall1((long)CHOS_SYS_MKDIR, (long)(uintptr_t)path);
}
static inline long chos_write_file(const char *path, const void *data, size_t length) {
    return chos_syscall3((long)CHOS_SYS_WRITE_FILE, (long)(uintptr_t)path,
                         (long)(uintptr_t)data, (long)length);
}
static inline long chos_unlink(const char *path) {
    return chos_syscall1((long)CHOS_SYS_UNLINK, (long)(uintptr_t)path);
}
static inline long chos_rmdir(const char *path) {
    return chos_syscall1((long)CHOS_SYS_RMDIR, (long)(uintptr_t)path);
}
/* Nonblocking, process-owned outbound TCP client. IPv4 addresses use network
 * presentation order in a uint32_t (e.g. 10.0.2.2 == 0x0a000202). */
static inline long chos_map_memory(size_t bytes){return chos_syscall2(CHOS_SYS_MAP_MEMORY,(long)bytes,0);}
static inline long chos_unmap_memory(void *base,size_t bytes){return chos_syscall2(CHOS_SYS_UNMAP_MEMORY,(long)(uintptr_t)base,(long)bytes);}
static inline long chos_network_status(chos_net_status_t *out){return chos_syscall2(CHOS_SYS_NET_INFO,(long)(uintptr_t)out,sizeof(*out));}
static inline long chos_capabilities(void){return chos_syscall0(CHOS_SYS_CAPABILITIES);}
static inline long chos_spawn_limited(const char *path,const char *args,uint64_t mask){return chos_syscall3(CHOS_SYS_SPAWN_LIMITED,(long)(uintptr_t)path,(long)(uintptr_t)args,(long)mask);}
static inline long chos_ipc_connect(uint32_t peer){return chos_syscall1(CHOS_SYS_IPC_OPEN,peer);}
static inline long chos_ipc_get_status(uint64_t h,chos_ipc_info_t *out){return chos_syscall3(CHOS_SYS_IPC_STATUS,(long)h,(long)(uintptr_t)out,sizeof(*out));}
static inline long chos_ipc_write(uint64_t h,const void *p,size_t n){return chos_syscall3(CHOS_SYS_IPC_SEND,(long)h,(long)(uintptr_t)p,(long)n);}
static inline long chos_ipc_read(uint64_t h,void *p,size_t n){return chos_syscall3(CHOS_SYS_IPC_RECV,(long)h,(long)(uintptr_t)p,(long)n);}
static inline long chos_ipc_release(uint64_t h){return chos_syscall1(CHOS_SYS_IPC_CLOSE,(long)h);}
static inline long chos_thread_create(const chos_thread_request_t *r){return chos_syscall2(CHOS_SYS_THREAD_CREATE,(long)(uintptr_t)r,sizeof(*r));}
static inline long chos_thread_status(uint32_t tid,chos_thread_info_t *out){return chos_syscall3(CHOS_SYS_THREAD_INFO,tid,(long)(uintptr_t)out,sizeof(*out));}
static inline long chos_thread_collect(uint32_t tid){return chos_syscall1(CHOS_SYS_THREAD_REAP,tid);}
static inline long chos_thread_id(void){return chos_syscall0(CHOS_SYS_THREAD_SELF);}
static inline void *chos_thread_tls(void){uintptr_t p;__asm__ volatile("mrs %0,tpidr_el0":"=r"(p));return (void *)p;}
static inline __attribute__((noreturn)) void chos_thread_exit(int64_t status){chos_syscall1(CHOS_SYS_THREAD_EXIT,status);for(;;)__asm__ volatile("wfe");}
static inline long chos_clock_status(chos_clock_info_t *out) {return chos_syscall2(CHOS_SYS_CLOCK_INFO,(uint64_t)(uintptr_t)out,sizeof(*out));}
static inline long chos_network_status6(chos_net6_info_t *out){return chos_syscall2(CHOS_SYS_NET6_INFO,(long)(uintptr_t)out,sizeof(*out));}
static inline long chos_ping6(const char *literal){return chos_syscall1(CHOS_SYS_NET6_PING,(long)(uintptr_t)literal);}

static inline long chos_net_tcp_open(uint16_t local_port) {
    return chos_syscall1((long)CHOS_SYS_NET_OPEN, (long)local_port);
}
static inline long chos_net_tcp_connect(int handle, uint32_t remote_ipv4, uint16_t remote_port) {
    return chos_syscall3((long)CHOS_SYS_NET_CONNECT, (long)handle,
                         (long)remote_ipv4, (long)remote_port);
}
static inline long chos_net_tcp_status(int handle, chos_tcp_socket_info_t *out) {
    return chos_syscall2((long)CHOS_SYS_NET_STATUS, (long)handle, (long)(uintptr_t)out);
}
static inline long chos_net_tcp_send(int handle, const void *buffer, size_t length) {
    return chos_syscall3((long)CHOS_SYS_NET_SEND, (long)handle,
                         (long)(uintptr_t)buffer, (long)length);
}
static inline long chos_net_tcp_recv(int handle, void *buffer, size_t capacity) {
    return chos_syscall3((long)CHOS_SYS_NET_RECV, (long)handle,
                         (long)(uintptr_t)buffer, (long)capacity);
}
static inline long chos_net_tcp_close(int handle) {
    return chos_syscall1((long)CHOS_SYS_NET_CLOSE, (long)handle);
}
static inline long chos_net_tcp_abort(int handle) {
    return chos_syscall1((long)CHOS_SYS_NET_ABORT, (long)handle);
}
/* Owner-bound IPv6 sockets. Connect fails closed without actual entropy; this
 * never changes the existing IPv4 transport or network configuration. */
static inline long chos_net_tcp6_open(uint16_t port){return chos_syscall1(CHOS_SYS_NET6_OPEN,port);}
static inline long chos_net_tcp6_connect(int h,const uint8_t peer[16],uint16_t port){return chos_syscall3(CHOS_SYS_NET6_CONNECT,h,(long)(uintptr_t)peer,port);}
static inline long chos_net_tcp6_status(int h,chos_tcp6_socket_info_t *out){return chos_syscall3(CHOS_SYS_NET6_STATUS,h,(long)(uintptr_t)out,sizeof(*out));}
static inline long chos_net_tcp6_send(int h,const void *p,size_t n){return chos_syscall3(CHOS_SYS_NET6_SEND,h,(long)(uintptr_t)p,(long)n);}
static inline long chos_net_tcp6_recv(int h,void *p,size_t n){return chos_syscall3(CHOS_SYS_NET6_RECV,h,(long)(uintptr_t)p,(long)n);}
static inline long chos_net_tcp6_close(int h){return chos_syscall1(CHOS_SYS_NET6_CLOSE,h);}
static inline long chos_net_tcp6_abort(int h){return chos_syscall1(CHOS_SYS_NET6_ABORT,h);}
static inline long chos_net_dns6_lookup(const char *host){return chos_syscall1(CHOS_SYS_NET6_DNS_START,(long)(uintptr_t)host);}
static inline long chos_net_dns6_status(chos_dns6_info_t *out){return chos_syscall2(CHOS_SYS_NET6_DNS_STATUS,(long)(uintptr_t)out,sizeof(*out));}
/* Global asynchronous DNS A-query service; one query may be pending system-wide. */
static inline long chos_net_dns_lookup(const char *hostname) {
    return chos_syscall1((long)CHOS_SYS_NET_DNS_START, (long)(uintptr_t)hostname);
}
static inline long chos_net_dns_status(chos_dns_api_info_t *out) {
    return chos_syscall1((long)CHOS_SYS_NET_DNS_STATUS, (long)(uintptr_t)out);
}

static inline long chos_fsync(void) { return chos_syscall0((long)CHOS_SYS_FSYNC); }
static inline long chos_rename(const char *old_path, const char *new_path) {
    return chos_syscall2((long)CHOS_SYS_RENAME, (long)(uintptr_t)old_path, (long)(uintptr_t)new_path);
}
/* Queue an embedded ROMFS application for asynchronous EL0 execution. */
static inline long chos_spawn(const char *absolute_path) {
    return chos_syscall1((long)CHOS_SYS_SPAWN, (long)(uintptr_t)absolute_path);
}
/* Launch with a bounded space-separated argv tail; paths should not contain spaces. */
static inline long chos_spawn_args(const char *absolute_path, const char *arguments) {
    return chos_syscall2((long)CHOS_SYS_SPAWN_ARGS, (long)(uintptr_t)absolute_path,
                         (long)(uintptr_t)arguments);
}
/* Nonblocking read from the current Console output stream; -EAGAIN means empty. */
static inline long chos_console_read(void *buffer, size_t capacity) {
    return chos_syscall2((long)CHOS_SYS_CONSOLE_READ, (long)(uintptr_t)buffer, (long)capacity);
}

/* Submit a versioned GUI protocol message or poll one queued GUI event. The
 * poll is nonblocking; -EAGAIN means the application should yield/sleep. */
static inline long chos_gui_submit(const chos_gui_message_t *message, size_t bytes) {
    return chos_syscall2((long)CHOS_SYS_GUI_SUBMIT, (long)(uintptr_t)message, (long)bytes);
}
static inline long chos_gui_poll_event(chos_gui_message_t *event, size_t capacity) {
    return chos_syscall2((long)CHOS_SYS_GUI_POLL_EVENT, (long)(uintptr_t)event, (long)capacity);
}

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

static inline __attribute__((noreturn)) void chos_exit(int status) {
    (void)chos_syscall1((long)CHOS_SYS_EXIT, (long)status);
    for (;;) __asm__ volatile("wfe");
}

#endif
