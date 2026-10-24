#ifndef CHOS_INSTALLER_H
#define CHOS_INSTALLER_H

#include <stdint.h>

#define CHOS_INSTALLER_SECTOR_SIZE 512u
#define CHOS_INSTALLER_ALIGNMENT_SECTORS 2048u /* 1 MiB for 512-byte logical sectors */
#define CHOS_INSTALLER_ESP_SECTORS 131072u      /* 64 MiB FAT32 EFI System Partition */
#define CHOS_INSTALLER_MIN_DATA_SECTORS 131072u /* 64 MiB minimum CHOS data partition */
#define CHOS_INSTALLER_CONFIRMATION "ERASE CHOS TARGET"

typedef enum {
    CHOS_INSTALLER_STAGE_EMPTY = 0,
    CHOS_INSTALLER_STAGE_TARGET_SELECTED,
    CHOS_INSTALLER_STAGE_PLAN_READY,
    CHOS_INSTALLER_STAGE_CONFIRMED,
    CHOS_INSTALLER_STAGE_ESP_WRITTEN,
    CHOS_INSTALLER_STAGE_DATA_INITIALIZED,
    CHOS_INSTALLER_STAGE_VERIFIED,
    CHOS_INSTALLER_STAGE_COMPLETE,
    CHOS_INSTALLER_STAGE_FAILED
} chos_installer_stage_t;

typedef struct {
    uint64_t disk_identity;
    uint64_t disk_sector_count;
    uint32_t logical_sector_size;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint64_t esp_first_lba;
    uint64_t esp_last_lba;
    uint64_t data_first_lba;
    uint64_t data_last_lba;
    uint64_t plan_cookie;
    chos_installer_stage_t stage;
    uint8_t requires_destructive_confirmation;
    uint8_t confirmed;
} chos_installer_plan_t;

/* Builds and validates a non-destructive installation plan only. These helpers
 * never write disk sectors. identity is an opaque stable identifier from the
 * eventual device manager (not just a human-readable drive name). */
int chos_installer_prepare_plan(uint64_t disk_identity, uint64_t disk_sector_count,
                                uint32_t logical_sector_size,
                                chos_installer_plan_t *plan_out);
int chos_installer_validate_plan(const chos_installer_plan_t *plan);
int chos_installer_confirm_plan(chos_installer_plan_t *plan,
                                uint64_t currently_selected_disk_identity,
                                const char *confirmation_text);
int chos_installer_plan_is_confirmed(const chos_installer_plan_t *plan,
                                     uint64_t currently_selected_disk_identity);
int chos_installer_transition(chos_installer_plan_t *plan, chos_installer_stage_t next_stage);

#endif
