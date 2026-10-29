#ifndef CHOS_PROCESS_H
#define CHOS_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"
#include "chos/elf_image.h"
#include "chos/process_caps.h"
#include "chos/file_api.h"

#define CHOS_PROCESS_MAX 4u
#define CHOS_PROCESS_NAME_CAPACITY 16u
#define CHOS_PROCESS_MAX_ARGS 8u
#define CHOS_PROCESS_MAX_OPEN_FILES CHOS_FILE_OPEN_MAX
#define CHOS_PROCESS_FD_BASE 3
#define CHOS_PROCESS_ARG_MAX_LENGTH 96u
#define CHOS_EXEC_V1_HEADER_SIZE 24u
#define CHOS_EXEC_V2_HEADER_SIZE 56u
#define CHOS_EXEC_HEADER_SIZE CHOS_EXEC_V1_HEADER_SIZE
#define CHOS_EXEC_VERSION 1u
#define CHOS_EXEC_VERSION_SEGMENTS 2u
#define CHOS_EXEC_VERSION_ELF 0x10000u
#define CHOS_PROCESS_IMAGE_PAGES (CHOS_ELF_MAX_SPAN / 4096u)
#define CHOS_EXEC_MAX_IMAGE_PAGES 16u
#define CHOS_EXEC_MAX_IMAGE_SIZE (CHOS_EXEC_MAX_IMAGE_PAGES * 4096u)
#define CHOS_EXEC_MAX_STACK_PAGES 8u
#define CHOS_EXEC_MIN_STACK_PAGES 1u

/* CHCX v1 is a flat RX image; v2 describes page-separated RX, R, and RW ranges.
 * All on-disk integer fields are little-endian. v2's BSS is zero-filled by loader. */
typedef struct {
    uint32_t version;
    uint32_t image_size;       /* initialized payload bytes, excluding header */
    uint32_t entry_offset;
    uint16_t stack_pages;
    uint16_t header_size;
    uint32_t code_size;        /* v2: code starts at virtual offset zero */
    uint32_t rodata_offset;
    uint32_t rodata_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t bss_offset;
    uint32_t bss_size;
    uint32_t virtual_size;     /* page-rounded v2 image address span */
    size_t image_offset;
    chos_elf_image_t elf;
} chos_exec_image_info_t;

int process_image_validate(const void *file_bytes, size_t file_size,
                           chos_exec_image_info_t *out);
int process_image_write_header(void *destination, size_t capacity,
                               uint32_t image_size, uint32_t entry_offset,
                               uint16_t stack_pages);
int process_image_write_v2_header(void *destination, size_t capacity,
                                  uint32_t image_size, uint32_t entry_offset,
                                  uint16_t stack_pages, uint32_t code_size,
                                  uint32_t rodata_offset, uint32_t rodata_size,
                                  uint32_t data_offset, uint32_t data_size,
                                  uint32_t bss_offset, uint32_t bss_size,
                                  uint32_t virtual_size);

typedef enum {
    CHOS_PROCESS_EMPTY = 0,
    CHOS_PROCESS_LOADING,
    CHOS_PROCESS_QUEUED,
    CHOS_PROCESS_RUNNING,
    CHOS_PROCESS_EXITED,
    CHOS_PROCESS_FAILED
} chos_process_state_t;

typedef enum {
    PROCESS_OK = 0,
    PROCESS_ERR_ARGUMENT,
    PROCESS_ERR_FORMAT,
    PROCESS_ERR_CONTEXT,
    PROCESS_ERR_CAPACITY,
    PROCESS_ERR_ADDRESS_SPACE,
    PROCESS_ERR_OUT_OF_MEMORY,
    PROCESS_ERR_MAPPING,
    PROCESS_ERR_EXECUTION,
    PROCESS_ERR_CLEANUP,
    PROCESS_ERR_NOT_FOUND,
    PROCESS_ERR_ROMFS_CORRUPT,
    PROCESS_ERR_STORAGE_IO
} chos_process_error_t;

typedef struct {
    uint32_t pid;
    chos_process_state_t state;
    int64_t exit_status;
    uint32_t address_space_id;
    uint32_t image_pages;
    uint32_t stack_pages;
    uint64_t entry_address;
    uint64_t user_stack_top;
    uint32_t scheduler_task_id;
    int cleanup_pending;
    uint64_t fault_esr;
    uint64_t fault_elr;
    uint64_t fault_far;
    char name[CHOS_PROCESS_NAME_CAPACITY];
} chos_process_info_t;

typedef struct {
    uint32_t pid;
    int64_t exit_status;
    int completed;
} chos_process_result_t;

typedef struct {
    uint64_t started;
    uint64_t exited;
    uint64_t failed;
} chos_process_stats_t;

typedef struct {
    size_t history_records;
    size_t active_processes;
    size_t reusable_terminal_records;
    size_t pending_cleanup;
} chos_process_table_stats_t;

/* Loads a validated CHCX/native CHOS ELF image from kernel memory and runs it synchronously
 * in EL0. The image buffer is copied into private pages before execution. */
chos_process_error_t process_run_image(const char *name, const void *file_bytes,
                                       size_t file_size, chos_process_result_t *result);
chos_process_error_t process_run_demo(void);
/* Load a packaged read-only CHCX file by absolute ROMFS path. */
chos_process_error_t process_run_path(const char *path, chos_process_result_t *result);
chos_process_error_t process_spawn_path(const char *path, uint32_t *pid_out);
chos_process_error_t process_spawn_path_args(const char *path, const char *arguments, uint32_t *pid_out);
/* Resolve a live process by its bound address space; returns 0 when not found. */
uint32_t process_pid_for_address_space(address_space_handle_t space);
/* Kernel-owned main stack bounds for a live address-space generation. */
int process_main_stack_bounds(address_space_handle_t,uint64_t *,uint64_t *);
/* Queue the async ROMFS demo applications and run them under timer preemption. */
int process_async_self_test(void);
/* Record lower-EL synchronous fault registers against the foreground or scheduled process. */
void process_record_user_fault(uint64_t esr, uint64_t elr, uint64_t far);
/* Validate and execute multiple packaged CHCX entries, including a fault-isolation probe. */
int process_romfs_self_test(void);
/* Spawn an EL0 process as a scheduler task. Image bytes are copied during this call.
 * The process runs when scheduler_run() is called. */
chos_process_error_t process_spawn_image(const char *name, const void *file_bytes,
                                         size_t file_size, uint32_t *pid_out);
chos_process_error_t process_spawn_image_args(const char *name, const void *file_bytes,
                                         size_t file_size, const char *const argv[],
                                         size_t argc, uint32_t *pid_out);
/* Verify timer-preempted EL0 execution and isolate one deliberate user fault. */
int process_preemptive_isolation_test(void);
chos_process_error_t process_self_test(void);
void process_reap_pending(void);
/* EL0 process creation is serviced by the scheduled kernel runtime task. The
 * syscall copies arguments into this bounded queue, yields while waiting, and
 * receives the real PID/status after the kernel task performs the privileged load. */
int process_get_capabilities(address_space_handle_t,uint64_t *);
uint32_t process_spawn_request_enqueue_policy(const char *,const char *,int,uint64_t);
uint32_t process_spawn_request_enqueue(const char *path, const char *arguments, int with_arguments);
int process_spawn_request_status(uint32_t ticket, chos_process_error_t *error_out, uint32_t *pid_out);
int process_spawn_request_cancel(uint32_t ticket);
void process_spawn_request_release(uint32_t ticket);
int process_service_pending_spawn_request(void);
size_t process_count(void);
int process_get_info_at(size_t index, chos_process_info_t *out);
void process_get_stats(chos_process_stats_t *out);
void process_get_table_stats(chos_process_table_stats_t *out);
/* PID lookup preserves terminal status until its process-table slot is reused. */
int process_get_info_by_pid(uint32_t pid, chos_process_info_t *out);
/* Returns 1 while live, 0 when terminal, and -1 when the PID is unknown. */
int process_query_pid(uint32_t pid, chos_process_state_t *state_out, int64_t *status_out);
/* Read-only process-owned descriptors; fd values are local to the owning EL0 process. */
int64_t process_file_open_for_address_space(address_space_handle_t space, const char *path);
int64_t process_file_read_for_address_space(address_space_handle_t space, int fd,
                                            uint64_t user_destination, size_t length);
int64_t process_file_read_at_for_address_space(address_space_handle_t,int,uint64_t,size_t,uint64_t);
int process_file_info_for_address_space(address_space_handle_t,int,chos_file_info_t *);
int64_t process_file_seek_for_address_space(address_space_handle_t space,int fd,int64_t offset,unsigned origin);
int64_t process_file_close_for_address_space(address_space_handle_t space, int fd);
int process_slot_reuse_test(void);
const char *process_state_name(chos_process_state_t state);
const char *process_error_string(chos_process_error_t error);

#endif
