#include <stdint.h>
#include "chos/power.h"
#include "chos/uart.h"
#include "chos/storage.h"
#include "chos/timer.h"
static unsigned power_conduit;
extern int64_t chos_arch_psci(unsigned conduit,uint32_t function);
void chos_power_set_conduit(unsigned conduit) { power_conduit=conduit<=2u?conduit:0u; }
const char *chos_power_execute(chos_power_action_t action) {
    if(action!=CHOS_POWER_SHUTDOWN && action!=CHOS_POWER_RESTART)return "Invalid power action.";
    if(!power_conduit)return "Firmware power service unavailable; use the VM controls.";
    uint64_t frequency=timer_frequency_hz(),start=timer_counter();
    if(action==CHOS_POWER_SHUTDOWN && (!frequency || frequency>UINT64_MAX/5u))return "Shutdown clock unavailable; power-off cancelled.";
    if(storage_mounted() && storage_device() && storage_device()->write && !storage_sync())
        return "Storage flush failed; power action cancelled.";
    if(action==CHOS_POWER_SHUTDOWN) {
        uart_puts("[POWER] Shutting down... (minimum five seconds)\n");
        /* Architectural counter remains available without timer IRQ delivery. */
        while(timer_counter()-start<frequency*5u) {
#if defined(__aarch64__)
            uint64_t daif;__asm__ volatile("mrs %0,daif":"=r"(daif));
            if(timer_is_ready() && !(daif&128u))__asm__ volatile("wfi" ::: "memory");
            else __asm__ volatile("yield" ::: "memory");
#endif
        }
    }
    /* Scheduled applications may have run during the visible grace interval. */
    if(action==CHOS_POWER_SHUTDOWN && storage_mounted() && storage_device() && storage_device()->write && !storage_sync())
        return "Storage flush failed; shutdown cancelled.";
    uart_puts(action==CHOS_POWER_SHUTDOWN?"[POWER] PSCI system off.\n":"[POWER] PSCI system reset.\n");
    (void)chos_arch_psci(power_conduit,action==CHOS_POWER_SHUTDOWN?0x84000008u:0x84000009u);
    return "Firmware rejected the power request; use the VM controls.";
}
