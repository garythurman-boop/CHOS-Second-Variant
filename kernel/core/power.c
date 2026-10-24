#include <stdint.h>
#include "chos/power.h"
#include "chos/uart.h"

extern void chos_arch_restart(void) __attribute__((noreturn));
extern void chos_arch_halt(void) __attribute__((noreturn));

void chos_power_execute(chos_power_action_t action) {
    if (action == CHOS_POWER_RESTART) {
        uart_puts("[POWER] Restart requested; performing a CHOS software restart.\n");
        chos_arch_restart();
    }

    uart_puts("[POWER] Shutdown requested. This bare-metal QEMU boot path has no verified system-off service.\n");
    uart_puts("[POWER] CHOS is halting the CPU; close the virtual machine from the host to power it off.\n");
    chos_arch_halt();
}
