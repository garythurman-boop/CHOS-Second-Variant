#include "chos/installer.h"
#include <stddef.h>
#include <stdint.h>

#define GPT_ENTRY_ARRAY_SECTORS 32u
#define GPT_BACKUP_OVERHEAD_SECTORS (GPT_ENTRY_ARRAY_SECTORS + 1u)
#define GPT_PRIMARY_FIRST_USABLE (2u + GPT_ENTRY_ARRAY_SECTORS)
#define PLAN_COOKIE_SALT UINT64_C(0x43484f532d494e53)

static int text_equal(const char *a, const char *b) {
    size_t i = 0u;
    if (a == NULL || b == NULL) return 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == b[i];
}

static uint64_t align_up(uint64_t value, uint64_t alignment, int *ok) {
    uint64_t rem;
    if (alignment == 0u || ok == NULL) { if (ok != NULL) *ok = 0; return 0u; }
    rem = value % alignment;
    if (rem == 0u) return value;
    if (value > UINT64_MAX - (alignment - rem)) { *ok = 0; return 0u; }
    return value + alignment - rem;
}

static uint64_t plan_cookie(const chos_installer_plan_t *plan) {
    uint64_t x;
    if (plan == NULL) return 0u;
    x = PLAN_COOKIE_SALT ^ plan->disk_identity ^ plan->disk_sector_count;
    x ^= plan->esp_first_lba * UINT64_C(0x9e3779b185ebca87);
    x ^= plan->esp_last_lba * UINT64_C(0xc2b2ae3d27d4eb4f);
    x ^= plan->data_first_lba * UINT64_C(0x165667b19e3779f9);
    x ^= plan->data_last_lba * UINT64_C(0x85ebca77c2b2ae63);
    return x != 0u ? x : PLAN_COOKIE_SALT;
}

int chos_installer_prepare_plan(uint64_t disk_identity, uint64_t disk_sector_count,
                                uint32_t logical_sector_size,
                                chos_installer_plan_t *plan_out) {
    uint64_t last_usable, esp_last, data_first;
    int ok = 1;
    if (plan_out == NULL || disk_identity == 0u || logical_sector_size != CHOS_INSTALLER_SECTOR_SIZE ||
        disk_sector_count <= GPT_PRIMARY_FIRST_USABLE + GPT_BACKUP_OVERHEAD_SECTORS) return 0;
    last_usable = disk_sector_count - GPT_BACKUP_OVERHEAD_SECTORS - 1u;
    if (last_usable < GPT_PRIMARY_FIRST_USABLE) return 0;
    if (CHOS_INSTALLER_ESP_SECTORS - 1u > UINT64_MAX - CHOS_INSTALLER_ALIGNMENT_SECTORS) return 0;
    esp_last = (uint64_t)CHOS_INSTALLER_ALIGNMENT_SECTORS + CHOS_INSTALLER_ESP_SECTORS - 1u;
    data_first = align_up(esp_last + 1u, CHOS_INSTALLER_ALIGNMENT_SECTORS, &ok);
    if (!ok || data_first > last_usable || last_usable - data_first + 1u < CHOS_INSTALLER_MIN_DATA_SECTORS) return 0;

    plan_out->disk_identity = disk_identity;
    plan_out->disk_sector_count = disk_sector_count;
    plan_out->logical_sector_size = logical_sector_size;
    plan_out->first_usable_lba = GPT_PRIMARY_FIRST_USABLE;
    plan_out->last_usable_lba = last_usable;
    plan_out->esp_first_lba = CHOS_INSTALLER_ALIGNMENT_SECTORS;
    plan_out->esp_last_lba = esp_last;
    plan_out->data_first_lba = data_first;
    plan_out->data_last_lba = last_usable;
    plan_out->stage = CHOS_INSTALLER_STAGE_PLAN_READY;
    plan_out->requires_destructive_confirmation = 1u;
    plan_out->confirmed = 0u;
    plan_out->plan_cookie = plan_cookie(plan_out);
    return chos_installer_validate_plan(plan_out);
}

int chos_installer_validate_plan(const chos_installer_plan_t *plan) {
    if (plan == NULL || plan->disk_identity == 0u || plan->disk_sector_count == 0u ||
        plan->logical_sector_size != CHOS_INSTALLER_SECTOR_SIZE ||
        plan->first_usable_lba != GPT_PRIMARY_FIRST_USABLE ||
        plan->last_usable_lba >= plan->disk_sector_count ||
        plan->esp_first_lba < plan->first_usable_lba ||
        plan->esp_last_lba < plan->esp_first_lba ||
        plan->data_first_lba <= plan->esp_last_lba ||
        plan->data_last_lba < plan->data_first_lba ||
        plan->data_last_lba != plan->last_usable_lba ||
        plan->esp_first_lba % CHOS_INSTALLER_ALIGNMENT_SECTORS != 0u ||
        plan->data_first_lba % CHOS_INSTALLER_ALIGNMENT_SECTORS != 0u ||
        plan->esp_last_lba - plan->esp_first_lba + 1u != CHOS_INSTALLER_ESP_SECTORS ||
        plan->data_last_lba - plan->data_first_lba + 1u < CHOS_INSTALLER_MIN_DATA_SECTORS ||
        plan->requires_destructive_confirmation != 1u ||
        plan->stage < CHOS_INSTALLER_STAGE_PLAN_READY ||
        plan->stage > CHOS_INSTALLER_STAGE_FAILED ||
        (plan->stage < CHOS_INSTALLER_STAGE_CONFIRMED && plan->confirmed != 0u) ||
        (plan->stage >= CHOS_INSTALLER_STAGE_CONFIRMED && plan->stage != CHOS_INSTALLER_STAGE_FAILED && plan->confirmed != 1u) ||
        plan->confirmed > 1u ||
        plan->plan_cookie != plan_cookie(plan)) return 0;
    return 1;
}

int chos_installer_confirm_plan(chos_installer_plan_t *plan,
                                uint64_t currently_selected_disk_identity,
                                const char *confirmation_text) {
    if (!chos_installer_validate_plan(plan) || currently_selected_disk_identity == 0u ||
        currently_selected_disk_identity != plan->disk_identity ||
        !text_equal(confirmation_text, CHOS_INSTALLER_CONFIRMATION) ||
        plan->stage != CHOS_INSTALLER_STAGE_PLAN_READY || plan->confirmed != 0u) return 0;
    plan->confirmed = 1u;
    plan->stage = CHOS_INSTALLER_STAGE_CONFIRMED;
    return 1;
}

int chos_installer_plan_is_confirmed(const chos_installer_plan_t *plan,
                                     uint64_t currently_selected_disk_identity) {
    return chos_installer_validate_plan(plan) && plan->confirmed == 1u &&
           plan->stage >= CHOS_INSTALLER_STAGE_CONFIRMED &&
           plan->stage != CHOS_INSTALLER_STAGE_FAILED &&
           currently_selected_disk_identity != 0u &&
           currently_selected_disk_identity == plan->disk_identity;
}

int chos_installer_transition(chos_installer_plan_t *plan, chos_installer_stage_t next_stage) {
    if (!chos_installer_validate_plan(plan) || next_stage > CHOS_INSTALLER_STAGE_FAILED) return 0;
    if (next_stage == CHOS_INSTALLER_STAGE_FAILED) {
        if (plan->stage == CHOS_INSTALLER_STAGE_COMPLETE) return 0;
        plan->stage = CHOS_INSTALLER_STAGE_FAILED;
        return 1;
    }
    if (plan->stage == CHOS_INSTALLER_STAGE_FAILED || plan->stage == CHOS_INSTALLER_STAGE_COMPLETE) return 0;
    /* Only the explicit destructive-confirmation API may enter this state. */
    if (next_stage == CHOS_INSTALLER_STAGE_CONFIRMED) return 0;
    if (next_stage != (chos_installer_stage_t)(plan->stage + 1)) return 0;
    if (next_stage >= CHOS_INSTALLER_STAGE_ESP_WRITTEN && !plan->confirmed) return 0;
    plan->stage = next_stage;
    return 1;
}
