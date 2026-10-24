#include "chos/installer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const uint64_t mib256_sectors = UINT64_C(256) * 1024u * 1024u / 512u;
    const uint64_t identity = UINT64_C(0x43484f534449534b);
    chos_installer_plan_t plan;
    chos_installer_plan_t tampered;

    memset(&plan, 0, sizeof(plan));
    assert(chos_installer_prepare_plan(identity, mib256_sectors, 512u, &plan));
    assert(chos_installer_validate_plan(&plan));
    assert(plan.stage == CHOS_INSTALLER_STAGE_PLAN_READY);
    assert(plan.confirmed == 0u && !chos_installer_plan_is_confirmed(&plan, identity));
    assert(plan.esp_first_lba == 2048u);
    assert(plan.esp_last_lba == 133119u);
    assert(plan.data_first_lba == 133120u);
    assert(plan.data_last_lba == 524254u);
    assert(!chos_installer_confirm_plan(&plan, identity, "ERASE DISK"));
    assert(!chos_installer_confirm_plan(&plan, identity + 1u, CHOS_INSTALLER_CONFIRMATION));
    assert(!chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_ESP_WRITTEN));
    assert(!chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_CONFIRMED));

    assert(chos_installer_confirm_plan(&plan, identity, CHOS_INSTALLER_CONFIRMATION));
    assert(chos_installer_plan_is_confirmed(&plan, identity));
    assert(!chos_installer_plan_is_confirmed(&plan, identity + 1u));
    assert(chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_ESP_WRITTEN));
    assert(chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_DATA_INITIALIZED));
    assert(!chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_COMPLETE)); /* cannot skip verify */
    assert(chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_VERIFIED));
    assert(chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_COMPLETE));
    assert(!chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_FAILED));

    memset(&tampered, 0, sizeof(tampered));
    assert(chos_installer_prepare_plan(identity, mib256_sectors, 512u, &tampered));
    tampered.data_first_lba += 1u;
    assert(!chos_installer_validate_plan(&tampered));
    assert(!chos_installer_prepare_plan(identity, mib256_sectors, 4096u, &tampered));
    assert(!chos_installer_prepare_plan(0u, mib256_sectors, 512u, &tampered));
    assert(!chos_installer_prepare_plan(identity, 200000u, 512u, &tampered));

    memset(&plan, 0, sizeof(plan));
    assert(chos_installer_prepare_plan(identity, mib256_sectors, 512u, &plan));
    assert(chos_installer_confirm_plan(&plan, identity, CHOS_INSTALLER_CONFIRMATION));
    assert(chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_ESP_WRITTEN));
    assert(chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_FAILED));
    assert(!chos_installer_plan_is_confirmed(&plan, identity));
    assert(!chos_installer_transition(&plan, CHOS_INSTALLER_STAGE_DATA_INITIALIZED));

    puts("Installer plan: GPT geometry, target identity, destructive confirmation and monotonic stage gates passed");
    return 0;
}
