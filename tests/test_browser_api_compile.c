#include <stdint.h>
#include "chos/user.h"

/* Freestanding ABI compile probe: browser uses the shared asynchronous DNS
 * service and process-owned TCP client through these public wrappers. */
long chos_test_browser_api_compile_probe(const char *hostname,
                                         uint32_t server_ipv4,
                                         char *response_buffer) {
    chos_dns_api_info_t dns;
    chos_tcp_socket_info_t tcp;
    long handle;
    long rc = chos_net_dns_lookup(hostname);
    (void)chos_net_dns_status(&dns);
    handle = chos_net_tcp_open(0u);
    if (handle < 1) return rc;
    rc = chos_net_tcp_connect((int)handle, server_ipv4, 80u);
    (void)chos_net_tcp_status((int)handle, &tcp);
    (void)chos_net_tcp_send((int)handle, "GET / HTTP/1.0\r\n\r\n", 18u);
    (void)chos_net_tcp_recv((int)handle, response_buffer, 128u);
    (void)chos_net_tcp_close((int)handle);
    (void)chos_net_tcp_abort((int)handle);
    return rc;
}
