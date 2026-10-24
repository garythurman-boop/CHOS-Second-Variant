#include <stdint.h>
#include "chos/atomic.h"
#include "chos/timer.h"

#define TIMER_RELOAD_MAX 0x7fffffffu

static volatile uint64_t tick_count;
static uint64_t counter_frequency;
static uint32_t reload_value;
static int ready;

static uint64_t read_cntfrq(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

uint64_t timer_counter(void) {
    uint64_t value;
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static void write_timer_control(uint64_t value) {
    __asm__ volatile("msr cntp_ctl_el0, %0\n\tisb" :: "r"(value) : "memory");
}

static void write_timer_value(uint32_t value) {
    uint64_t wide_value = value;
    __asm__ volatile("msr cntp_tval_el0, %0\n\tisb" :: "r"(wide_value) : "memory");
}

int timer_init(void) {
    uint64_t interval;

    ready = 0;
    tick_count = 0u;
    counter_frequency = read_cntfrq();
    if (counter_frequency == 0u) return 0;

    interval = counter_frequency / CHOS_TIMER_HZ;
    if (interval == 0u) interval = 1u;
    if (interval > TIMER_RELOAD_MAX) return 0;
    reload_value = (uint32_t)interval;

    /* Disable while programming, then enable periodic one-shot reloads in IRQ. */
    write_timer_control(0u);
    write_timer_value(reload_value);
    __asm__ volatile("dsb sy" ::: "memory");
    write_timer_control(1u); /* ENABLE=1, IMASK=0 */
    ready = 1;
    return 1;
}

void timer_stop(void) {
    /* Used as an IRQ-storm circuit breaker if the GIC reports a bad IAR ID. */
    write_timer_control(0u);
    __asm__ volatile("dsb sy" ::: "memory");
    ready = 0;
}

int timer_is_ready(void) {
    return ready;
}

uint64_t timer_ticks(void) {
    return chos_atomic_load_u64(&tick_count);
}

uint64_t timer_frequency_hz(void) {
    /* The architectural counter runs even when the periodic IRQ path could not be enabled. */
    return counter_frequency != 0u ? counter_frequency : read_cntfrq();
}

void timer_handle_irq(void) {
    if (!ready) return;
    (void)chos_atomic_fetch_add_u64(&tick_count, 1u);
    write_timer_value(reload_value);
}
