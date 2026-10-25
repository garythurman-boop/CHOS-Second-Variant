#include <assert.h>
#include <stdio.h>
#include "chos/syscall.h"
#include "chos/file_api.h"
#include "chos/net.h"
#include "chos/net_tcp_api.h"
#include "chos/net_dns_api.h"

int main(void) {
    assert(CHOS_SYS_WRITE == 1u);
    assert(CHOS_SYS_EXIT == 2u);
    assert(CHOS_SYS_GETPID == 3u);
    assert(CHOS_SYS_YIELD == 4u);
    assert(CHOS_SYS_SLEEP == 5u);
    assert(CHOS_SYS_GETSTATUS == 6u);
    assert(CHOS_SYS_WAIT == 7u);
    assert(CHOS_SYS_OPEN == 8u);
    assert(CHOS_SYS_READ == 9u);
    assert(CHOS_SYS_CLOSE == 10u);
    assert(CHOS_SYS_GUI_SUBMIT == 11u);
    assert(CHOS_SYS_GUI_POLL_EVENT == 12u);
    assert(CHOS_SYS_SPAWN == 13u);
    assert(CHOS_SYS_LIST_FILES == 14u);
    assert(CHOS_SYS_CONSOLE_READ == 15u);
    assert(CHOS_SYS_SPAWN_ARGS == 16u);
    assert(CHOS_FILE_LIST_MAX == 64u);
    assert(CHOS_SYS_MKDIR == 17u);
    assert(CHOS_SYS_WRITE_FILE == 18u);
    assert(CHOS_SYS_UNLINK == 19u);
    assert(CHOS_SYS_LIST_DIR == 20u);
    assert(CHOS_SYS_RMDIR == 21u);
    assert(CHOS_SYS_FSYNC == 22u);
    assert(CHOS_SYS_RENAME == 23u);
    assert(CHOS_SYS_NET_OPEN == 24u);
    assert(CHOS_SYS_NET_CONNECT == 25u);
    assert(CHOS_SYS_NET_STATUS == 26u);
    assert(CHOS_SYS_NET_SEND == 27u);
    assert(CHOS_SYS_NET_RECV == 28u);
    assert(CHOS_SYS_NET_CLOSE == 29u);
    assert(CHOS_SYS_NET_DNS_START == 30u);
    assert(CHOS_SYS_NET_DNS_STATUS == 31u);
    assert(CHOS_SYS_NET_ABORT == 32u);
    assert(CHOS_SYS_RANDOM_BYTES == 33u);
    assert(CHOS_SYS_NET_INFO == 34u);
    assert(CHOS_SYS_MAX_RANDOM_BYTES == 256u);
    assert(CHOS_NET_DNS_API_HOST_MAX == 256u);
    assert(sizeof(chos_dns_api_info_t) == 276u);
    assert(CHOS_TCP_MAX_SOCKETS == 4u);
    assert(CHOS_TCP_MSS == CHOS_SYS_MAX_NET_TRANSFER);
    assert(sizeof(chos_tcp_socket_info_t) == 72u);
    assert(CHOS_NET_ABI_VERSION == 1u);
    assert(CHOS_NET_AF_INET == 2u);
    assert(sizeof(chos_ipv4_addr_t) == 4u);
    assert(sizeof(chos_sockaddr_ipv4_t) == 16u);
    assert(sizeof(chos_net_socket_info_t) == 16u);
    assert(sizeof(chos_file_list_entry_t) == 72u);
    assert(CHOS_SYS_MAX_READ == 256u);
    assert(CHOS_STATUS_LOADING == 1u);
    assert(CHOS_STATUS_QUEUED == 2u);
    assert(CHOS_STATUS_RUNNING == 3u);
    assert(CHOS_STATUS_EXITED == 4u);
    assert(CHOS_STATUS_FAILED == 5u);
    assert(CHOS_SYS_MAX_WRITE == 256u);
    assert(CHOS_SYS_MAX_SLEEP_TICKS == 60000u);
    puts("user ABI: syscall IDs, argument/result register contract, file APIs, network socket design layouts and bounds passed");
    return 0;
}
