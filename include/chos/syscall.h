#ifndef CHOS_SYSCALL_H
#define CHOS_SYSCALL_H

/* CHOS AArch64 EL0 syscall ABI, selected in x8 and invoked with SVC #0.
 * Arguments use x0-x5; result or negative errno returns in x0. */
#define CHOS_SYS_WRITE  1u  /* x0=user buffer, x1=byte count; returns count or -errno */
#define CHOS_SYS_EXIT   2u  /* x0=process status; never returns to EL0 */
#define CHOS_SYS_GETPID 3u  /* returns the current process ID or -ESRCH */
#define CHOS_SYS_YIELD  4u  /* voluntarily yields this scheduled user task */
#define CHOS_SYS_SLEEP  5u  /* x0=ticks to sleep; timer-driven, maximum is below */
#define CHOS_SYS_GETSTATUS 6u /* x0=PID; returns process state or -errno */
#define CHOS_SYS_WAIT      7u /* x0=PID; waits for terminal state, returns exit status */
#define CHOS_SYS_OPEN      8u /* x0=user NUL-terminated path; returns process-owned read-only fd */
#define CHOS_SYS_READ      9u /* x0=fd, x1=user buffer, x2=byte count; returns bytes or -errno */
#define CHOS_SYS_CLOSE    10u /* x0=process-owned fd; releases handle */
#define CHOS_SYS_GUI_SUBMIT    11u /* x0=user GUI message, x1=message bytes; returns object ID/status */
#define CHOS_SYS_GUI_POLL_EVENT 12u /* x0=user message buffer, x1=capacity; returns event size or -EAGAIN */
#define CHOS_SYS_SPAWN 13u /* x0=user absolute ROMFS path; returns new PID or -errno */
#define CHOS_SYS_LIST_FILES 14u /* x0=user entry array, x1=entry capacity; returns entries copied */
#define CHOS_SYS_CONSOLE_READ 15u /* x0=user buffer, x1=capacity; active EL0 Console output or -EAGAIN */
#define CHOS_SYS_SPAWN_ARGS 16u /* x0=user executable path, x1=user space-separated argument string; returns PID or -errno */
#define CHOS_SYS_MKDIR 17u /* x0=user directory path; persistent CHFS directory */
#define CHOS_SYS_WRITE_FILE 18u /* x0=path, x1=data, x2=size; atomic replace/create, max 64 KiB per call */
#define CHOS_SYS_UNLINK 19u /* x0=user file path; remove persistent file */
#define CHOS_SYS_LIST_DIR 20u /* x0=directory path, x1=entry array, x2=capacity */
#define CHOS_SYS_RMDIR 21u /* x0=user empty directory path */
#define CHOS_SYS_FSYNC 22u /* flush persistent storage */
#define CHOS_SYS_RENAME 23u /* x0=old path, x1=new path; rename/move a persistent file or directory subtree */
#define CHOS_SYS_NET_OPEN 24u /* x0=local port (0=ephemeral); returns process-owned TCP handle */
#define CHOS_SYS_NET_CONNECT 25u /* x0=handle, x1=IPv4 host-order uint32, x2=remote port; nonblocking */
#define CHOS_SYS_NET_STATUS 26u /* x0=handle, x1=user chos_tcp_socket_info_t output */
#define CHOS_SYS_NET_SEND 27u /* x0=handle, x1=user bytes, x2=length <= 512 */
#define CHOS_SYS_NET_RECV 28u /* x0=handle, x1=user writable bytes, x2=capacity <= 512 */
#define CHOS_SYS_NET_CLOSE 29u /* x0=handle; graceful nonblocking close */
#define CHOS_SYS_NET_DNS_START 30u /* x0=NUL-terminated hostname; starts the global async DNS A query */
#define CHOS_SYS_NET_DNS_STATUS 31u /* x0=user writable chos_dns_api_info_t output */
#define CHOS_SYS_NET_ABORT 32u /* x0=handle; aborts and releases a process-owned TCP socket */
#define CHOS_SYS_RANDOM_BYTES 33u /* x0=user writable buffer, x1=capacity; nonblocking hardware-backed entropy */
#define CHOS_SYS_NET_INFO 34u /* x0=writable chos_net_status_t, x1=capacity; read-only OS status */
#define CHOS_SYS_MAP_MEMORY 35u /* x0=bytes (1..128KiB), x1=flags0; zeroed process RW/XN pages */
#define CHOS_SYS_UNMAP_MEMORY 36u /* x0=exact mapping base, x1=rounded allocation length */
#define CHOS_SYS_NET6_INFO 37u /* x0=writable chos_net6_info_t, x1=capacity; read-only control-plane status */
#define CHOS_SYS_NET6_PING 38u /* x0=NUL-terminated IPv6 literal; one bounded OS-owned echo request */
#define CHOS_SYS_NET6_OPEN 39u /* x0=local port (0=ephemeral), process-owned nonblocking TCP6 */
#define CHOS_SYS_NET6_CONNECT 40u /* x0=handle,x1=16 network-order IPv6 bytes,x2=port */
#define CHOS_SYS_NET6_STATUS 41u /* x0=handle,x1=chos_tcp6_socket_info_t,x2=capacity */
#define CHOS_SYS_NET6_SEND 42u /* x0=handle,x1=buffer,x2=length<=512 */
#define CHOS_SYS_NET6_RECV 43u /* x0=handle,x1=writable buffer,x2=capacity<=512 */
#define CHOS_SYS_NET6_CLOSE 44u /* x0=handle, starts bounded graceful FIN */
#define CHOS_SYS_NET6_ABORT 45u /* x0=handle, immediate owner-only release */
#define CHOS_SYS_NET6_DNS_START 46u /* x0=hostname; owner-bound async AAAA over validated RDNSS server */
#define CHOS_SYS_NET6_DNS_STATUS 47u /* x0=writable chos_dns6_info_t,x1=capacity */
#define CHOS_SYS_CLOCK_INFO 48u /* x0=writable chos_clock_info_t,x1=capacity; read-only clocks */
#define CHOS_SYS_IPC_OPEN 49u /* x0=live peerPID; mutual consent before communication */
#define CHOS_SYS_IPC_STATUS 50u /* x0=owner handle,x1=status,x2=capacity */
#define CHOS_SYS_IPC_SEND 51u /* x0=handle,x1=bytes,x2=length1..512 */
#define CHOS_SYS_IPC_RECV 52u /* x0=handle,x1=bytes,x2=capacity1..512 */
#define CHOS_SYS_IPC_CLOSE 53u /* x0=owner handle */
#define CHOS_SYS_SPAWN_LIMITED 54u /* x0=path,x1=arguments,x2=reduced inherited syscall mask */
#define CHOS_SYS_CAPABILITIES 55u /* returns own effective mask; always permitted */
#define CHOS_SYS_THREAD_CREATE 56u /* x0=64-byte request,x1=capacity; scheduled shared-AS worker */
#define CHOS_SYS_THREAD_INFO 57u /* x0=own thread TID,x1=32-byte output,x2=capacity */
#define CHOS_SYS_THREAD_REAP 58u /* x0=own exited TID; releases join record */
#define CHOS_SYS_THREAD_EXIT 59u /* x0=64-bit result; secondary thread only */
#define CHOS_SYS_THREAD_SELF 60u /* stable own thread ID, including the main task */
#define CHOS_SYS_FUTEX_WAIT 61u /* x0=private RW word,x1=uint32 expected,x2=relative timeout ticks */
#define CHOS_SYS_FUTEX_WAKE 62u /* x0=private RW word,x1=max waiters0..8; returns count */
#define CHOS_SYS_SEEK 63u /* x0=owned read-only fd,x1=signed offset,x2=SET0/CUR1/END2; shares READ permission */
#define CHOS_SYSCALL_LIMIT CHOS_SYS_SEEK
#define CHOS_STATUS_LOADING 1u
#define CHOS_STATUS_QUEUED  2u
#define CHOS_STATUS_RUNNING 3u
#define CHOS_STATUS_EXITED  4u
#define CHOS_STATUS_FAILED  5u
#define CHOS_SYS_MAX_WRITE 256u
#define CHOS_SYS_MAX_READ  256u
#define CHOS_SYS_MAX_FILE_WRITE (64u * 1024u)
#define CHOS_SYS_MAX_NET_TRANSFER 512u
#define CHOS_SYS_MAX_RANDOM_BYTES 256u
#define CHOS_SYS_MAX_SLEEP_TICKS 60000u /* 10 minutes at CHOS's 100 Hz timer */

#define CHOS_ERRNO_EINTR 4
#define CHOS_ERRNO_ETIMEDOUT 110
#define CHOS_ERRNO_EPIPE 32
#define CHOS_ERRNO_EMSGSIZE 90
#define CHOS_ERRNO_EPERM 1
#define CHOS_ERRNO_ENOENT 2
#define CHOS_ERRNO_EEXIST 17
#define CHOS_ERRNO_ENOTDIR 20
#define CHOS_ERRNO_EISDIR 21
#define CHOS_ERRNO_ENOSPC 28
#define CHOS_ERRNO_EBUSY 16
#define CHOS_ERRNO_ENOTEMPTY 39
#define CHOS_ERRNO_EIO 5
#define CHOS_ERRNO_EAGAIN 11
#define CHOS_ERRNO_EBADF 9
#define CHOS_ERRNO_ENOMEM 12
#define CHOS_ERRNO_EMFILE 24
#define CHOS_ERRNO_ESRCH 3
#define CHOS_ERRNO_E2BIG 7
#define CHOS_ERRNO_EFAULT 14
#define CHOS_ERRNO_EINVAL 22
#define CHOS_ERRNO_ENOSYS 38
#define CHOS_ERRNO_EOVERFLOW 75
#define CHOS_ERRNO_ECHILD 10
#define CHOS_ERRNO_EDEADLK 35

#endif
