#ifndef CHOS_IPC_H
#define CHOS_IPC_H
#include <stddef.h>
#include <stdint.h>
#define CHOS_IPC_VERSION 1u
#define CHOS_IPC_CHANNELS 8u
#define CHOS_IPC_OWNER_LIMIT 4u
#define CHOS_IPC_MESSAGE_MAX 512u
#define CHOS_IPC_QUEUE_DEPTH 4u
#define CHOS_IPC_CONNECTED 1u
#define CHOS_IPC_PEER_CLOSED 2u
typedef struct {
 uint32_t version,size_bytes;
 uint64_t handle;
 uint32_t peer_pid,flags,queued_messages,next_message_bytes;
 uint64_t sent_bytes,received_bytes,reserved[2];
} chos_ipc_info_t;
_Static_assert(sizeof(chos_ipc_info_t)==64u,"IPC status ABI");
/* Nonblocking mutual-PID rendezvous, not POSIX sockets/descriptor passing.
 * The syscall validates that both PIDs are real live processes. */
int64_t chos_ipc_open(uint32_t owner,uint32_t peer);
int chos_ipc_status(uint32_t owner,uint64_t handle,chos_ipc_info_t *);
int chos_ipc_send(uint32_t owner,uint64_t handle,const void *,size_t);
int chos_ipc_receive(uint32_t owner,uint64_t handle,void *,size_t);
int chos_ipc_close(uint32_t owner,uint64_t handle);
void chos_ipc_drop_owner(uint32_t owner);
#endif
