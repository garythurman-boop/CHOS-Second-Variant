#ifndef CHOS_POWER_H
#define CHOS_POWER_H

typedef enum {
    CHOS_POWER_SHUTDOWN = 1,
    CHOS_POWER_RESTART = 2
} chos_power_action_t;

/* Platform policy: these actions do not return. Shutdown halts the CPU when
 * firmware does not provide a verified system-off service; restart performs a
 * kernel software restart and reinitializes CHOS from its entry point. */
void chos_power_execute(chos_power_action_t action) __attribute__((noreturn));

#endif
