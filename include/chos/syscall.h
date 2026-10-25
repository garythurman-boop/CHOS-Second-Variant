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
#define CHOS_ERRNO_ECHILD 10
#define CHOS_ERRNO_EDEADLK 35

#endif
