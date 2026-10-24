#ifndef CHOS_INSTALLER_IO_H
#define CHOS_INSTALLER_IO_H
#include <stddef.h>
#include "chos/installer.h"
#include "chos/blockdev.h"
/* Snapshot must query the live device, not just return the selected label. */
typedef int (*chos_install_snapshot_fn)(uint64_t *identity,uint64_t *sectors);
typedef enum {INSTALL_IDLE,INSTALL_SELECTED,INSTALL_WRITING,INSTALL_FORMATTING,INSTALL_VERIFYING,INSTALL_DONE,INSTALL_FAILED,INSTALL_CANCELLED} chos_install_state_t;
typedef struct {
    chos_installer_plan_t plan;
    chos_install_state_t state;
    chos_block_device_t *disk;
    chos_install_snapshot_fn snapshot;
    const uint8_t *payload;
    size_t payload_size;
    uint64_t next_sector;
    const char *error;
} chos_install_job_t;
int chos_install_select(chos_install_job_t *job,chos_block_device_t *disk,chos_install_snapshot_fn snapshot,const void *payload,size_t bytes);
int chos_install_confirm(chos_install_job_t *job,const char *text);
void chos_install_step(chos_install_job_t *job);
/* Cancellation is terminal; a partially written target is not rolled back. */
void chos_install_cancel(chos_install_job_t *job);
#endif
