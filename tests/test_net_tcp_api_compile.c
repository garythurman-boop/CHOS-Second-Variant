#include <stddef.h>
#include <stdint.h>
#include "chos/user.h"

/* Compiled freestanding, not executed by the host. Ensures each public TCP
 * wrapper is type-correct and emits the declared syscall ABI. */
long chos_test_tcp_api_compile_probe(uint32_t remote_ipv4, const char *request,
                                     char *receive_buffer) {
    chos_tcp_socket_info_t info;
    long handle = chos_net_tcp_open(0u);
    long rc;
    if (handle < 1) return handle;
    rc = chos_net_tcp_connect((int)handle, remote_ipv4, 80u);
    if (rc == 0) {
        (void)chos_net_tcp_status((int)handle, &info);
        (void)chos_net_tcp_send((int)handle, request, chos_strlen(request));
        (void)chos_net_tcp_recv((int)handle, receive_buffer, 128u);
    }
    (void)chos_net_tcp_close((int)handle);
    return rc;
}
