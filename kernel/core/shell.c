#include <stddef.h>
#include <stdint.h>
#include "chos/shell.h"
#include "chos/exception.h"
#include "chos/fdt.h"
#include "chos/platform.h"
#include "chos/entropy.h"
#include "chos/uart.h"
#include "chos/pmm.h"
#include "chos/kheap.h"
#include "chos/interrupt.h"
#include "chos/timer.h"
#include "chos/vmm.h"
#include "chos/address_space.h"
#include "chos/scheduler.h"
#include "chos/register_stress.h"
#include "chos/user_mode.h"
#include "chos/process.h"
#include "chos/romfs.h"
#include "chos/storage.h"
#include "chos/virtio_blk.h"
#include "chos/virtio_net.h"
#include "chos/net_stack.h"
#include "chos/net_manager.h"
#include "chos/net_tcp.h"
#include "chos/gui.h"
#include "chos/virtio_input.h"

#define CHOS_VERSION "0.30.0"
#define LINE_CAPACITY 128u

static volatile int shell_runtime_active;
static volatile int shell_runtime_exit_requested;
static void shell_loop(const fdt_info_t *device_tree, int runtime_mode);
static void enter_runtime_session(const fdt_info_t *device_tree);
static void runtime_shell_task_entry(void *argument);

static int strings_equal(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == b[i];
}

static int starts_with(const char *s, const char *prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

static char *skip_spaces(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    return s;
}

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    uart_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (unsigned)shift) & 0x0fu]);
    }
}

static void print_decimal(size_t value) {
    char digits[24];
    size_t n = 0;
    if (value == 0u) {
        uart_putc('0');
        return;
    }
    while (value != 0u && n < sizeof(digits)) {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0u) uart_putc(digits[--n]);
}

static void print_signed64(int64_t value) {
    uint64_t magnitude;
    char digits[24];
    size_t n = 0u;
    if (value < 0) {
        uart_putc('-');
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint64_t)value;
    }
    if (magnitude == 0u) { uart_putc('0'); return; }
    while (magnitude != 0u && n < sizeof(digits)) {
        digits[n++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }
    while (n > 0u) uart_putc(digits[--n]);
}

/* Use only while holding uart_write_begin()/uart_write_end(). */
static void print_decimal_locked(size_t value) {
    uart_put_u64_locked((uint64_t)value);
}

typedef struct {
    const char *label;
    size_t steps;
} scheduler_demo_context_t;

static scheduler_demo_context_t scheduler_demo_a = {"A", 3u};
static scheduler_demo_context_t scheduler_demo_b = {"B", 3u};

static void scheduler_demo_task(void *argument) {
    scheduler_demo_context_t *context = (scheduler_demo_context_t *)argument;
    size_t step;
    for (step = 1u; step <= context->steps; ++step) {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[TASK ");
        uart_puts_locked(context->label);
        uart_puts_locked("] step ");
        print_decimal_locked(step);
        uart_puts_locked("\n");
        uart_write_end(flags);
        scheduler_yield();
    }
    {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[TASK ");
        uart_puts_locked(context->label);
        uart_puts_locked("] exiting\n");
        uart_write_end(flags);
    }
}

static void print_scheduler_info(void) {
    scheduler_stats_t stats;
    size_t i;
    scheduler_task_info_t task;

    scheduler_get_stats(&stats);
    uart_puts("CHOS scheduler\n");
    uart_puts("Mode: ");
    uart_puts(stats.preemption_enabled ? "timer preemption requested (development mode)\n" : "cooperative (default; preemption disabled)\n");
    uart_puts("Initialized: "); uart_puts(stats.initialized ? "yes\n" : "no\n");
    uart_puts("Currently running scheduler: "); uart_puts(stats.running ? "yes\n" : "no\n");
    uart_puts("Task capacity: "); print_decimal(stats.task_capacity);
    uart_puts("; stack per task: "); print_decimal(stats.stack_bytes_per_task); uart_puts(" bytes");
    uart_puts("; unmapped guard: "); print_decimal(stats.stack_guard_bytes_per_task); uart_puts(" bytes\n");
    uart_puts("Sleeping tasks: "); print_decimal(stats.sleeping_tasks);
    uart_puts("; sleep calls: "); print_decimal((size_t)stats.sleep_calls);
    uart_puts("; wakeups: "); print_decimal((size_t)stats.wakeups); uart_puts("\n");
    uart_puts("Scheduler critical sections: "); print_decimal((size_t)stats.critical_sections); uart_puts("\n");
    uart_puts("Tasks created: "); print_decimal((size_t)stats.created_tasks);
    uart_puts("; completed: "); print_decimal((size_t)stats.completed_tasks); uart_puts("\n");
    uart_puts("Deferred cleanup: reaped "); print_decimal((size_t)stats.reaped_tasks);
    uart_puts("; pending "); print_decimal(stats.pending_reaps); uart_puts("\n");
    uart_puts("Yield calls: "); print_decimal((size_t)stats.yield_calls);
    uart_puts("; context switches: "); print_decimal((size_t)stats.context_switches); uart_puts("\n");
    uart_puts("Timer IRQs during preemption mode: "); print_decimal((size_t)stats.preemption_irqs); uart_puts("\n");
    uart_puts("Timer-driven preemptions: "); print_decimal((size_t)stats.preemption_switches);
    uart_puts("; saved IRQ frames: "); print_decimal((size_t)stats.preempted_frames); uart_puts("\n");
    uart_puts("Address-space tasks: "); print_decimal(stats.address_space_tasks);
    uart_puts("; TTBR0 root switches: "); print_decimal((size_t)stats.address_space_switches);
    uart_puts("; switch failures: "); print_decimal((size_t)stats.address_space_switch_failures); uart_puts("\n");
    uart_puts("Task list:\n");
    for (i = 0u; i < scheduler_get_task_count(); ++i) {
        if (scheduler_get_task_info(i, &task)) {
            uart_puts("  id="); print_decimal(task.id); uart_puts(" name=");
            uart_puts(task.name); uart_puts(" state=");
            uart_puts(scheduler_state_name(task.state));
            if (task.has_address_space) {
                uart_puts(" address-space="); print_decimal(task.address_space_id);
            } else {
                uart_puts(" address-space=kernel");
            }
            uart_puts(" stack="); print_hex64(task.stack_base_address);
            uart_puts(" guard="); print_hex64(task.stack_guard_address);
            if (task.state == SCHED_TASK_SLEEPING) {
                uart_puts(" wake_tick="); print_decimal((size_t)task.wake_tick);
            }
            uart_puts("\n");
        }
    }
}

static void run_scheduler_test(void) {
    scheduler_stats_t before, after;
    uint32_t task_a_id, task_b_id;
    if (scheduler_is_running()) {
        uart_puts("[FAIL] Scheduler is already running\n");
        return;
    }
    scheduler_get_stats(&before);
    uart_puts("Running two cooperative tasks; output should alternate A/B.\n");
    if (!scheduler_create_task("demo-A", scheduler_demo_task, &scheduler_demo_a, &task_a_id)) {
        uart_puts("[FAIL] Could not create demo-A task\n");
        return;
    }
    if (!scheduler_create_task("demo-B", scheduler_demo_task, &scheduler_demo_b, &task_b_id)) {
        uart_puts("[FAIL] Could not create demo-B task; running queued task to completion\n");
        (void)scheduler_run();
        return;
    }
    uart_puts("Created task IDs "); print_decimal(task_a_id); uart_puts(" and ");
    print_decimal(task_b_id); uart_puts("\n");
    if (!scheduler_run()) {
        uart_puts("[FAIL] Scheduler refused to run\n");
        return;
    }
    scheduler_get_stats(&after);
    if (after.completed_tasks >= before.completed_tasks + 2u &&
        after.reaped_tasks >= before.reaped_tasks + 2u && after.pending_reaps == 0u) {
        uart_puts("[ OK ] cooperative task run returned to shell; deferred cleanup completed\n");
    } else {
        uart_puts("[FAIL] task completion or deferred cleanup counters did not advance\n");
    }
}

typedef struct {
    const char *label;
    uint64_t ticks;
} scheduler_sleep_demo_context_t;

typedef struct {
    volatile uint64_t *progress;
    uint64_t duration_ticks;
} scheduler_preempt_demo_context_t;

static volatile uint64_t preempt_progress_a;
static volatile uint64_t preempt_progress_b;
static scheduler_preempt_demo_context_t preempt_demo_a = {&preempt_progress_a, 8u};
static scheduler_preempt_demo_context_t preempt_demo_b = {&preempt_progress_b, 8u};
typedef struct {
    uint64_t ticks;
    volatile uint64_t *wake_tick;
} scheduler_preempt_sleep_context_t;
static volatile uint64_t preempt_sleep_wake_a;
static volatile uint64_t preempt_sleep_wake_b;
static scheduler_preempt_sleep_context_t preempt_sleep_demo_a = {5u, &preempt_sleep_wake_a};
static scheduler_preempt_sleep_context_t preempt_sleep_demo_b = {2u, &preempt_sleep_wake_b};

static scheduler_sleep_demo_context_t sleep_demo_a = {"A", 5u};
static scheduler_sleep_demo_context_t sleep_demo_b = {"B", 2u};

static void scheduler_sleep_demo_task(void *argument) {
    scheduler_sleep_demo_context_t *context = (scheduler_sleep_demo_context_t *)argument;
    {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[SLEEP "); uart_puts_locked(context->label); uart_puts_locked("] sleeping for ");
        print_decimal_locked((size_t)context->ticks); uart_puts_locked(" timer ticks\n");
        uart_write_end(flags);
    }
    scheduler_sleep_ticks(context->ticks);
    {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[SLEEP "); uart_puts_locked(context->label); uart_puts_locked("] woke and is exiting\n");
        uart_write_end(flags);
    }
}

static void run_scheduler_sleep_test(void) {
    scheduler_stats_t before, after;
    uint32_t task_a_id, task_b_id;
    if (scheduler_is_running()) {
        uart_puts("[FAIL] Scheduler is already running\n");
        return;
    }
    scheduler_get_stats(&before);
    uart_puts("Testing sleeping tasks and timer-driven wakeups; task B sleeps less than A.\n");
    if (!scheduler_create_task("sleep-A", scheduler_sleep_demo_task, &sleep_demo_a, &task_a_id)) {
        uart_puts("[FAIL] Could not create sleep-A task\n");
        return;
    }
    if (!scheduler_create_task("sleep-B", scheduler_sleep_demo_task, &sleep_demo_b, &task_b_id)) {
        uart_puts("[FAIL] Could not create sleep-B task\n");
        (void)scheduler_run();
        return;
    }
    scheduler_run();
    scheduler_get_stats(&after);
    if (after.completed_tasks >= before.completed_tasks + 2u &&
        after.reaped_tasks >= before.reaped_tasks + 2u && after.pending_reaps == 0u &&
        after.sleep_calls >= before.sleep_calls + 2u &&
        after.wakeups >= before.wakeups + 2u && !after.running &&
        after.sleeping_tasks == 0u) {
        uart_puts("[ OK ] task sleep/wakeup, idle wait and scheduler return passed\n");
    } else {
        uart_puts("[FAIL] sleep-test counters/state did not return to baseline\n");
    }
    (void)task_a_id;
    (void)task_b_id;
}

static void scheduler_preempt_demo_task(void *argument) {
    scheduler_preempt_demo_context_t *context = (scheduler_preempt_demo_context_t *)argument;
    uint64_t start;
    /* Exercise voluntary yield once, then run without further yields. */
    scheduler_yield();
    start = timer_ticks();
    while ((timer_ticks() - start) < context->duration_ticks) {
        *context->progress = *context->progress + 1u;
    }
}

static void run_preemptive_test(void) {
    scheduler_stats_t before, after;
    uint32_t task_a_id, task_b_id;

    if (scheduler_is_running()) {
        uart_puts("[FAIL] Scheduler is already running\n");
        return;
    }
    if (!timer_is_ready()) {
        uart_puts("[FAIL] Timer unavailable; preemptive test not enabled\n");
        return;
    }
    scheduler_get_stats(&before);
    preempt_progress_a = 0u;
    preempt_progress_b = 0u;
    uart_puts("Starting development preemption test. Each task yields once, then runs CPU-bound without further yields; expect timer IRQ context switches.\n");
    if (!scheduler_create_task("preempt-A", scheduler_preempt_demo_task, &preempt_demo_a, &task_a_id)) {
        uart_puts("[FAIL] Could not create preempt-A task\n");
        return;
    }
    if (!scheduler_create_task("preempt-B", scheduler_preempt_demo_task, &preempt_demo_b, &task_b_id)) {
        uart_puts("[FAIL] Could not create preempt-B task\n");
        (void)scheduler_run();
        return;
    }
    if (!scheduler_set_preemption_enabled(1)) {
        uart_puts("[FAIL] Could not enable development preemption\n");
        (void)scheduler_run();
        return;
    }
    if (!scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] Preemptive scheduler refused to run\n");
        return;
    }
    scheduler_get_stats(&after);
    uart_puts("Task A progress: "); print_decimal((size_t)preempt_progress_a); uart_puts("\n");
    uart_puts("Task B progress: "); print_decimal((size_t)preempt_progress_b); uart_puts("\n");
    uart_puts("Timer IRQs during test: "); print_decimal((size_t)(after.preemption_irqs - before.preemption_irqs)); uart_puts("\n");
    uart_puts("Timer-driven task preemptions: "); print_decimal((size_t)(after.preemption_switches - before.preemption_switches)); uart_puts("\n");
    if (preempt_progress_a > 0u && preempt_progress_b > 0u &&
        after.preemption_irqs > before.preemption_irqs &&
        after.preemption_switches > before.preemption_switches &&
        after.completed_tasks >= before.completed_tasks + 2u &&
        after.reaped_tasks >= before.reaped_tasks + 2u && after.pending_reaps == 0u &&
        !after.running && !after.preemption_enabled) {
        uart_puts("[ OK ] timer preempted CPU-bound tasks; IRQ-frame switching and deferred cleanup passed\n");
    } else {
        uart_puts("[FAIL] preemption counters, task progress or cleanup did not meet expectations\n");
    }
    (void)task_a_id;
    (void)task_b_id;
}

static void scheduler_preempt_sleep_task(void *argument) {
    scheduler_preempt_sleep_context_t *context = (scheduler_preempt_sleep_context_t *)argument;
    scheduler_sleep_ticks(context->ticks);
    *context->wake_tick = timer_ticks();
}

static void run_preemptive_sleep_test(void) {
    scheduler_stats_t before, after;
    uint32_t task_a_id, task_b_id;

    if (scheduler_is_running()) {
        uart_puts("[FAIL] Scheduler is already running\n");
        return;
    }
    if (!timer_is_ready()) {
        uart_puts("[FAIL] Timer unavailable; preemptive sleep test not enabled\n");
        return;
    }
    scheduler_get_stats(&before);
    preempt_sleep_wake_a = 0u;
    preempt_sleep_wake_b = 0u;
    uart_puts("Testing sleep/wakeup with timer preemption enabled; B sleeps fewer ticks than A.\n");
    if (!scheduler_create_task("preempt-slp-A", scheduler_preempt_sleep_task, &preempt_sleep_demo_a, &task_a_id)) {
        uart_puts("[FAIL] Could not create preemptive sleep-A task\n");
        return;
    }
    if (!scheduler_create_task("preempt-slp-B", scheduler_preempt_sleep_task, &preempt_sleep_demo_b, &task_b_id)) {
        uart_puts("[FAIL] Could not create preemptive sleep-B task\n");
        (void)scheduler_run();
        return;
    }
    if (!scheduler_set_preemption_enabled(1)) {
        uart_puts("[FAIL] Could not enable development preemption\n");
        (void)scheduler_run();
        return;
    }
    if (!scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] Preemptive scheduler refused to run\n");
        return;
    }
    scheduler_get_stats(&after);
    uart_puts("Task B wake tick: "); print_decimal((size_t)preempt_sleep_wake_b); uart_puts("\n");
    uart_puts("Task A wake tick: "); print_decimal((size_t)preempt_sleep_wake_a); uart_puts("\n");
    if (after.completed_tasks >= before.completed_tasks + 2u &&
        after.reaped_tasks >= before.reaped_tasks + 2u && after.pending_reaps == 0u &&
        after.sleep_calls >= before.sleep_calls + 2u &&
        after.wakeups >= before.wakeups + 2u &&
        after.preemption_irqs > before.preemption_irqs &&
        preempt_sleep_wake_b != 0u && preempt_sleep_wake_a > preempt_sleep_wake_b &&
        !after.running && !after.preemption_enabled && after.sleeping_tasks == 0u) {
        uart_puts("[ OK ] preemptive task sleep/wakeup, idle frame resume and scheduler return passed\n");
    } else {
        uart_puts("[FAIL] preemptive sleep/wakeup counters or cleanup did not meet expectations\n");
    }
    (void)task_a_id;
    (void)task_b_id;
}

#define REGISTER_STRESS_ITERATIONS UINT64_C(25000000)
#define CONSOLE_STRESS_LINES 6u

typedef struct {
    volatile uint32_t passed;
    volatile uint32_t failed;
} register_stress_result_t;

static register_stress_result_t register_stress_a;
static register_stress_result_t register_stress_b;

static void register_stress_task(void *argument) {
    register_stress_result_t *result = (register_stress_result_t *)argument;
    if (chos_register_preservation_probe(REGISTER_STRESS_ITERATIONS) == 0) {
        ++result->passed;
    } else {
        ++result->failed;
    }
}

static void run_register_preservation_test(void) {
    scheduler_stats_t before, after;
    uint32_t task_a_id, task_b_id;
    uint64_t ticks_before, ticks_after;

    if (scheduler_is_running() || !timer_is_ready()) {
        uart_puts("[FAIL] Scheduler already running or timer unavailable\n");
        return;
    }
    scheduler_get_stats(&before);
    ticks_before = timer_ticks();
    register_stress_a.passed = register_stress_a.failed = 0u;
    register_stress_b.passed = register_stress_b.failed = 0u;
    uart_puts("Testing x0-x30 and SP preservation across timer-preempted assembly loops...\n");
    if (!scheduler_create_task("regstress-A", register_stress_task,
                               &register_stress_a, &task_a_id)) {
        uart_puts("[FAIL] Could not create register-stress task A\n");
        return;
    }
    if (!scheduler_create_task("regstress-B", register_stress_task,
                               &register_stress_b, &task_b_id)) {
        uart_puts("[FAIL] Could not create register-stress task B\n");
        (void)scheduler_run();
        return;
    }
    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] Could not run register-preservation test with preemption\n");
        return;
    }
    scheduler_get_stats(&after);
    ticks_after = timer_ticks();
    uart_puts("Task A probes passed/failed: "); print_decimal(register_stress_a.passed);
    uart_putc('/'); print_decimal(register_stress_a.failed); uart_puts("\n");
    uart_puts("Task B probes passed/failed: "); print_decimal(register_stress_b.passed);
    uart_putc('/'); print_decimal(register_stress_b.failed); uart_puts("\n");
    uart_puts("Timer preemption switches during probe: ");
    print_decimal((size_t)(after.preemption_switches - before.preemption_switches)); uart_puts("\n");
    if (register_stress_a.passed == 1u && register_stress_a.failed == 0u &&
        register_stress_b.passed == 1u && register_stress_b.failed == 0u &&
        after.preemption_switches >= before.preemption_switches + 2u &&
        after.preemption_irqs > before.preemption_irqs && ticks_after > ticks_before &&
        after.completed_tasks >= before.completed_tasks + 2u &&
        after.reaped_tasks >= before.reaped_tasks + 2u && after.pending_reaps == 0u &&
        !after.running && !after.preemption_enabled) {
        uart_puts("[ OK ] x0-x30, SP, and IRQ exception-return state survived timer-preempted probes\n");
    } else {
        uart_puts("[FAIL] register values, timer switching, or task cleanup did not meet expectations\n");
    }
    (void)task_a_id;
    (void)task_b_id;
}

typedef struct {
    char label;
    volatile uint32_t lines_written;
} console_stress_context_t;

static console_stress_context_t console_stress_a = {'A', 0u};
static console_stress_context_t console_stress_b = {'B', 0u};

static void console_stress_task(void *argument) {
    console_stress_context_t *context = (console_stress_context_t *)argument;
    uint32_t line;
    for (line = 1u; line <= CONSOLE_STRESS_LINES; ++line) {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[CONSOLE ");
        uart_putc_locked(context->label);
        uart_puts_locked("] line ");
        uart_put_u64_locked(line);
        uart_puts_locked(" atomic\n");
        uart_write_end(flags);
        ++context->lines_written;

        /* Wait CPU-bound for the next timer tick; this creates real preemption
           points between output transactions without yielding voluntarily. */
        {
            uint64_t start_tick = timer_ticks();
            while (timer_ticks() == start_tick) {
                __asm__ volatile("yield" ::: "memory");
            }
        }
    }
}

static void run_console_stress_test(void) {
    scheduler_stats_t before, after;
    uint32_t task_a_id, task_b_id;

    if (scheduler_is_running() || !timer_is_ready()) {
        uart_puts("[FAIL] Scheduler already running or timer unavailable\n");
        return;
    }
    scheduler_get_stats(&before);
    console_stress_a.lines_written = console_stress_b.lines_written = 0u;
    uart_puts("Running atomic console writes from two timer-preempted tasks. Every line should remain intact.\n");
    if (!scheduler_create_task("console-A", console_stress_task, &console_stress_a, &task_a_id)) {
        uart_puts("[FAIL] Could not create console-stress task A\n");
        return;
    }
    if (!scheduler_create_task("console-B", console_stress_task, &console_stress_b, &task_b_id)) {
        uart_puts("[FAIL] Could not create console-stress task B\n");
        (void)scheduler_run();
        return;
    }
    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] Could not run console stress with preemption\n");
        return;
    }
    scheduler_get_stats(&after);
    uart_puts("Console A lines: "); print_decimal(console_stress_a.lines_written); uart_puts("\n");
    uart_puts("Console B lines: "); print_decimal(console_stress_b.lines_written); uart_puts("\n");
    if (console_stress_a.lines_written == CONSOLE_STRESS_LINES &&
        console_stress_b.lines_written == CONSOLE_STRESS_LINES &&
        after.preemption_switches >= before.preemption_switches + 2u &&
        after.completed_tasks >= before.completed_tasks + 2u &&
        after.reaped_tasks >= before.reaped_tasks + 2u && after.pending_reaps == 0u &&
        !after.running && !after.preemption_enabled) {
        uart_puts("[ OK ] concurrent console writers completed with atomic line transactions\n");
    } else {
        uart_puts("[FAIL] console lines, timer preemption, or deferred cleanup did not meet expectations\n");
    }
    (void)task_a_id;
    (void)task_b_id;
}


#define TASK_REUSE_ROUNDS 12u
#define TASK_REUSE_STEPS 16u

typedef struct {
    uint32_t round;
    volatile uint32_t done;
    volatile uint64_t checksum;
} task_reuse_context_t;

static task_reuse_context_t task_reuse_a;
static task_reuse_context_t task_reuse_b;

static void task_reuse_worker(void *argument) {
    task_reuse_context_t *context = (task_reuse_context_t *)argument;
    uint32_t step;
    uint64_t checksum = 0u;
    for (step = 0u; step < TASK_REUSE_STEPS; ++step) {
        checksum += (uint64_t)context->round + (uint64_t)step + 1u;
        if ((step & 3u) == 3u) scheduler_yield();
    }
    context->checksum = checksum;
    context->done = 1u;
}

static int find_task_stack_by_id(uint32_t task_id, uint64_t *stack_base) {
    size_t slot;
    scheduler_task_info_t info;
    if (stack_base == NULL || task_id == 0u) return 0;
    for (slot = 0u; slot < CHOS_SCHED_MAX_TASKS; ++slot) {
        if (scheduler_get_task_info(slot, &info) && info.id == task_id) {
            *stack_base = info.stack_base_address;
            return 1;
        }
    }
    return 0;
}

static void run_task_reuse_test(void) {
    scheduler_stats_t before, after;
    uint64_t first_stack_a = 0u, first_stack_b = 0u;
    uint32_t round;
    int ok = 1;

    if (scheduler_is_running()) {
        uart_puts("[FAIL] Scheduler is already running\n");
        return;
    }
    (void)scheduler_set_preemption_enabled(0);
    scheduler_get_stats(&before);
    uart_puts("Stress: repeated task creation, exit, deferred reaping and stack-slot reuse.\n");

    for (round = 0u; round < TASK_REUSE_ROUNDS; ++round) {
        uint32_t id_a = 0u, id_b = 0u;
        uint64_t stack_a = 0u, stack_b = 0u;

        task_reuse_a.round = round + 1u;
        task_reuse_a.done = 0u;
        task_reuse_a.checksum = 0u;
        task_reuse_b.round = round + 101u;
        task_reuse_b.done = 0u;
        task_reuse_b.checksum = 0u;

        if (!scheduler_create_task("reuse-A", task_reuse_worker, &task_reuse_a, &id_a) ||
            !scheduler_create_task("reuse-B", task_reuse_worker, &task_reuse_b, &id_b)) {
            ok = 0;
            if (id_a != 0u || id_b != 0u) (void)scheduler_run();
            break;
        }
        if (id_a == 0u || id_b == 0u || id_a == id_b || !scheduler_run()) {
            ok = 0;
            break;
        }
        scheduler_get_stats(&after);
        if (task_reuse_a.done != 1u || task_reuse_b.done != 1u ||
            task_reuse_a.checksum == 0u || task_reuse_b.checksum == 0u ||
            after.pending_reaps != 0u || after.running || after.preemption_enabled ||
            !find_task_stack_by_id(id_a, &stack_a) ||
            !find_task_stack_by_id(id_b, &stack_b) || stack_a == stack_b) {
            ok = 0;
            break;
        }
        if (round == 0u) {
            first_stack_a = stack_a;
            first_stack_b = stack_b;
        } else if (stack_a != first_stack_a || stack_b != first_stack_b) {
            /* The allocator should recycle the first two reaped slots in order. */
            ok = 0;
            break;
        }
    }

    scheduler_get_stats(&after);
    if (ok && round == TASK_REUSE_ROUNDS &&
        after.created_tasks >= before.created_tasks + 2u * TASK_REUSE_ROUNDS &&
        after.completed_tasks >= before.completed_tasks + 2u * TASK_REUSE_ROUNDS &&
        after.reaped_tasks >= before.reaped_tasks + 2u * TASK_REUSE_ROUNDS &&
        after.pending_reaps == 0u && !after.running && !after.preemption_enabled) {
        uart_puts("Rounds completed: "); print_decimal(TASK_REUSE_ROUNDS); uart_puts("; stacks reused at ");
        print_hex64(first_stack_a); uart_puts(" and "); print_hex64(first_stack_b); uart_puts("\n");
        uart_puts("[ OK ] repeated task create/exit/reap and safe stack-slot reuse passed\n");
    } else {
        uart_puts("[FAIL] task reuse invariants failed or a round did not complete\n");
    }
}

#define HEAP_PREEMPT_ITERATIONS 12u

typedef struct {
    uint32_t seed;
    volatile uint32_t operations;
    volatile uint32_t failures;
} heap_preempt_context_t;

static heap_preempt_context_t heap_preempt_a;
static heap_preempt_context_t heap_preempt_b;

static void heap_preempt_worker(void *argument) {
    heap_preempt_context_t *context = (heap_preempt_context_t *)argument;
    uint32_t iteration;
    for (iteration = 0u; iteration < HEAP_PREEMPT_ITERATIONS; ++iteration) {
        size_t size = 48u + (((size_t)context->seed * 131u + (size_t)iteration * 67u) % 768u);
        uint8_t pattern = (uint8_t)(0x43u + context->seed * 19u + iteration);
        uint8_t *payload = (uint8_t *)kheap_alloc(size);
        uint64_t start_tick;
        size_t i;
        int payload_ok = 1;

        if (payload == NULL) {
            ++context->failures;
            return;
        }
        for (i = 0u; i < size; ++i) payload[i] = (uint8_t)(pattern ^ (uint8_t)(i * 11u));

        /* Stay CPU-bound until an IRQ arrives so the other task allocates while
           this block is live. Neither task calls scheduler_yield(). */
        start_tick = timer_ticks();
        while (timer_ticks() == start_tick) __asm__ volatile("yield" ::: "memory");

        for (i = 0u; i < size; ++i) {
            if (payload[i] != (uint8_t)(pattern ^ (uint8_t)(i * 11u))) {
                payload_ok = 0;
                break;
            }
        }
        if (!payload_ok) ++context->failures;
        if (!kheap_free(payload)) ++context->failures;
        ++context->operations;
    }
}

static void run_heap_preempt_test(void) {
    scheduler_stats_t before_sched, after_sched;
    kheap_stats_t before_heap, after_heap;
    uint32_t task_a_id = 0u, task_b_id = 0u;
    int ok;

    if (scheduler_is_running() || !timer_is_ready()) {
        uart_puts("[FAIL] Scheduler already running or timer unavailable\n");
        return;
    }
    kheap_get_stats(&before_heap);
    if (!before_heap.initialized) {
        uart_puts("[FAIL] Kernel heap is not initialized\n");
        return;
    }
    scheduler_get_stats(&before_sched);
    heap_preempt_a.seed = 1u;
    heap_preempt_a.operations = heap_preempt_a.failures = 0u;
    heap_preempt_b.seed = 2u;
    heap_preempt_b.operations = heap_preempt_b.failures = 0u;

    uart_puts("Stress: two timer-preempted tasks allocating, verifying and freeing heap blocks.\n");
    if (!scheduler_create_task("heap-A", heap_preempt_worker, &heap_preempt_a, &task_a_id) ||
        !scheduler_create_task("heap-B", heap_preempt_worker, &heap_preempt_b, &task_b_id)) {
        if (task_a_id != 0u || task_b_id != 0u) (void)scheduler_run();
        uart_puts("[FAIL] Could not create both heap stress tasks\n");
        return;
    }
    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] Could not execute heap stress under timer preemption\n");
        return;
    }

    scheduler_get_stats(&after_sched);
    kheap_get_stats(&after_heap);
    ok = heap_preempt_a.operations == HEAP_PREEMPT_ITERATIONS && heap_preempt_a.failures == 0u &&
         heap_preempt_b.operations == HEAP_PREEMPT_ITERATIONS && heap_preempt_b.failures == 0u &&
         after_sched.preemption_switches > before_sched.preemption_switches &&
         after_sched.completed_tasks >= before_sched.completed_tasks + 2u &&
         after_sched.reaped_tasks >= before_sched.reaped_tasks + 2u &&
         after_sched.pending_reaps == 0u && !after_sched.running && !after_sched.preemption_enabled &&
         after_heap.allocated_blocks == before_heap.allocated_blocks &&
         after_heap.allocated_payload_bytes == before_heap.allocated_payload_bytes;

    uart_puts("Task A successful alloc/free cycles: "); print_decimal(heap_preempt_a.operations); uart_puts("; errors: ");
    print_decimal(heap_preempt_a.failures); uart_puts("\nTask B successful alloc/free cycles: ");
    print_decimal(heap_preempt_b.operations); uart_puts("; errors: "); print_decimal(heap_preempt_b.failures); uart_puts("\n");
    if (ok) {
        uart_puts("[ OK ] heap metadata stayed consistent under timer preemption; no outstanding test allocations\n");
    } else {
        uart_puts("[FAIL] heap stress, preemption, task cleanup or allocation baseline check failed\n");
    }
    (void)task_a_id;
    (void)task_b_id;
}

#define VMM_PREEMPT_ITERATIONS 12u

typedef struct {
    uint64_t seed;
    volatile uint32_t operations;
    volatile uint32_t failures;
} vmm_preempt_context_t;

static vmm_preempt_context_t vmm_preempt_a = {0x4141u, 0u, 0u};
static vmm_preempt_context_t vmm_preempt_b = {0x4242u, 0u, 0u};

static void vmm_preempt_worker(void *argument) {
    vmm_preempt_context_t *context = (vmm_preempt_context_t *)argument;
    uint32_t iteration;
    for (iteration = 0u; iteration < VMM_PREEMPT_ITERATIONS; ++iteration) {
        uint64_t physical = 0u, virtual_address = 0u, original = 0u;
        uint64_t pattern = 0x43484f5300000000ull ^ (context->seed << 16) ^ iteration;
        uint64_t start_tick;
        volatile uint64_t *identity;
        volatile uint64_t *alias;
        int have_page = 0, mapped = 0, safe_to_free = 1;

        if (!pmm_alloc_page(&physical)) {
            ++context->failures;
            return;
        }
        have_page = 1;
        identity = (volatile uint64_t *)(uintptr_t)physical;
        original = *identity;
        if (vmm_map_page_auto(physical, VMM_MEMORY_NORMAL,
                              VMM_PERM_KERNEL_DATA, &virtual_address) != VMM_OK) {
            ++context->failures;
            goto iteration_cleanup;
        }
        mapped = 1;
        alias = (volatile uint64_t *)(uintptr_t)virtual_address;
        *alias = pattern;
        if (*identity != pattern) ++context->failures;

        /* Hold this alias live across a timer preemption so another task runs
           the same find+map/unmap path and must receive a distinct VA. */
        start_tick = timer_ticks();
        while (timer_ticks() == start_tick) __asm__ volatile("yield" ::: "memory");
        if (*identity != pattern || *alias != pattern) ++context->failures;
        *identity = original;
        if (*alias != original) ++context->failures;
        if (vmm_unmap_page(virtual_address) == VMM_OK) {
            mapped = 0;
            if (vmm_query_mapping(virtual_address, &(vmm_mapping_info_t){0})) ++context->failures;
        } else {
            ++context->failures;
            safe_to_free = 0; /* Don't recycle a physical page behind a live alias. */
        }
        if (safe_to_free) ++context->operations;

iteration_cleanup:
        if (mapped) {
            *identity = original;
            if (vmm_unmap_page(virtual_address) != VMM_OK) safe_to_free = 0;
        }
        if (have_page && safe_to_free && !pmm_free_page(physical)) ++context->failures;
        if (!safe_to_free) ++context->failures;
    }
}

static void run_vmm_preempt_test(void) {
    scheduler_stats_t before_sched, after_sched;
    pmm_stats_t before_pmm, after_pmm;
    vmm_stats_t before_vmm, after_vmm;
    uint32_t task_a_id = 0u, task_b_id = 0u;
    int ok;

    if (scheduler_is_running() || !timer_is_ready() || !vmm_is_enabled()) {
        uart_puts("[FAIL] Scheduler already running, timer unavailable, or MMU inactive\n");
        return;
    }
    scheduler_get_stats(&before_sched);
    pmm_get_stats(&before_pmm);
    vmm_get_stats(&before_vmm);
    vmm_preempt_a.operations = vmm_preempt_a.failures = 0u;
    vmm_preempt_b.operations = vmm_preempt_b.failures = 0u;
    uart_puts("Stress: two timer-preempted tasks atomically mapping/unmapping PMM pages.\n");
    if (!scheduler_create_task("vmm-A", vmm_preempt_worker, &vmm_preempt_a, &task_a_id) ||
        !scheduler_create_task("vmm-B", vmm_preempt_worker, &vmm_preempt_b, &task_b_id)) {
        if (task_a_id != 0u || task_b_id != 0u) (void)scheduler_run();
        uart_puts("[FAIL] Could not create both VMM stress tasks\n");
        return;
    }
    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] Could not execute VMM stress under timer preemption\n");
        return;
    }

    scheduler_get_stats(&after_sched);
    pmm_get_stats(&after_pmm);
    vmm_get_stats(&after_vmm);
    ok = vmm_preempt_a.operations == VMM_PREEMPT_ITERATIONS && vmm_preempt_a.failures == 0u &&
         vmm_preempt_b.operations == VMM_PREEMPT_ITERATIONS && vmm_preempt_b.failures == 0u &&
         after_sched.preemption_switches > before_sched.preemption_switches &&
         after_sched.completed_tasks >= before_sched.completed_tasks + 2u &&
         after_sched.reaped_tasks >= before_sched.reaped_tasks + 2u &&
         after_sched.pending_reaps == 0u && !after_sched.running && !after_sched.preemption_enabled &&
         after_pmm.free_pages == before_pmm.free_pages &&
         after_pmm.allocated_pages == before_pmm.allocated_pages &&
         after_vmm.dynamic_mappings == before_vmm.dynamic_mappings;
    uart_puts("Task A map/unmap cycles: "); print_decimal(vmm_preempt_a.operations);
    uart_puts("; errors: "); print_decimal(vmm_preempt_a.failures);
    uart_puts("\nTask B map/unmap cycles: "); print_decimal(vmm_preempt_b.operations);
    uart_puts("; errors: "); print_decimal(vmm_preempt_b.failures); uart_puts("\n");
    if (ok) uart_puts("[ OK ] atomic dynamic VA allocation, alias visibility, TLB invalidation and PMM/VMM counters passed under preemption\n");
    else uart_puts("[FAIL] VMM preemption stress or page/mapping counter invariants failed\n");
    (void)task_a_id;
    (void)task_b_id;
}

static void print_address_space_info(void) {
    size_t i;
    address_space_info_t info;
    uint64_t current_root = vmm_current_root_table_address();
    uart_puts("CHOS process address-space status\n");
    uart_puts("Address-space slots: "); print_decimal(CHOS_AS_MAX_SPACES);
    uart_puts("; live: "); print_decimal(address_space_count()); uart_puts("\n");
    uart_puts("User virtual window: "); print_hex64(CHOS_AS_USER_VA_BASE);
    uart_puts(" - "); print_hex64(CHOS_AS_USER_VA_LIMIT - 1u); uart_puts(" (4 KiB pages)\n");
    uart_puts("User-frame limit per space: "); print_decimal(CHOS_AS_MAX_USER_FRAMES);
    uart_puts(" pages ("); print_decimal((CHOS_AS_MAX_USER_FRAMES * VMM_PAGE_SIZE) / 1024u);
    uart_puts(" KiB); EL0 entry: available through the controlled CHCX process runner; shell runs at EL1\n");
    uart_puts("Kernel root: "); print_hex64(vmm_kernel_root_table_address());
    uart_puts("; current TTBR0 root: "); print_hex64(current_root); uart_puts("\n");
    uart_puts(current_root == vmm_kernel_root_table_address()
              ? "Active root: kernel (default)\n"
              : "Active root: process address space (EL1-only activation test mode)\n");
    uart_puts("Root switching invalidates the stage-1 EL1/EL0 translation regime; ASIDs are disabled. EL0 runs only while a loaded process is executing.\n");
    for (i = 0u; i < CHOS_AS_MAX_SPACES; ++i) {
        if (address_space_get_info_at(i, &info)) {
            address_space_handle_t handle = {info.slot, info.generation, info.id};
            uart_puts("  slot="); print_decimal(info.slot);
            uart_puts(" id="); print_decimal(info.id);
            uart_puts(" generation="); print_decimal(info.generation);
            uart_puts(" root="); print_hex64(info.root_table_physical);
            uart_puts(address_space_is_active(handle) ? " [ACTIVE]" : "");
            uart_puts(" user-pages="); print_decimal(info.mapped_user_pages);
            uart_puts(" owned-frames="); print_decimal(info.owned_user_frames);
            uart_puts(" task-refs="); print_decimal(info.task_references);
            uart_puts(" L3-tables="); print_decimal(info.user_l3_tables);
            uart_puts("\n");
        }
    }
}

static void run_address_space_test(void) {
    pmm_stats_t before, after;
    int ok;
    if (scheduler_is_running()) {
        uart_puts("[FAIL] Cannot run address-space test while scheduler is active\n");
        return;
    }
    pmm_get_stats(&before);
    uart_puts("Testing cloned kernel mappings, private EL0-only descriptors, zeroed frame ownership and teardown.\n");
    ok = address_space_self_test();
    pmm_get_stats(&after);
    if (ok && before.free_pages == after.free_pages && before.allocated_pages == after.allocated_pages &&
        address_space_count() == 0u) {
        uart_puts("[ OK ] per-process roots, shared EL1-only kernel mappings, private user RO/RW+XN pages, ownership and teardown passed\n");
    } else {
        uart_puts("[FAIL] address-space tests failed or PMM/address-space counters did not return to baseline\n");
    }
}

static void run_address_space_switch_test(void) {
    pmm_stats_t before, after;
    uint64_t kernel_root = vmm_kernel_root_table_address();
    int ok;
    if (scheduler_is_running()) {
        uart_puts("[FAIL] Cannot switch TTBR0 roots while scheduler is active; per-task AS binding is not implemented\n");
        return;
    }
    if (vmm_current_root_table_address() != kernel_root) {
        uart_puts("[FAIL] Kernel TTBR0 root is not active before test\n");
        return;
    }
    pmm_get_stats(&before);
    uart_puts("Switching between two EL1-only address spaces at the same user VA; each switch flushes the ASID-0 TLB.\n");
    ok = address_space_activation_self_test();
    pmm_get_stats(&after);
    if (ok && vmm_current_root_table_address() == kernel_root &&
        before.free_pages == after.free_pages && before.allocated_pages == after.allocated_pages &&
        address_space_count() == 0u && vmm_self_test()) {
        uart_puts("[ OK ] TTBR0 activation/deactivation, full TLB invalidation, same-VA isolation, active-root query and teardown passed\n");
    } else {
        /* A test must never leave the shell executing on a temporary process root. */
        (void)address_space_deactivate();
        uart_puts("[FAIL] TTBR0 root-switch self-test or post-switch kernel VMM baseline failed\n");
    }
}


#define CHOS_TASK_AS_STRESS_ITERATIONS 12000000ull

typedef struct {
    volatile uint64_t *alias;
    uint64_t expected;
    uint64_t iterations;
    uint64_t failures;
} task_address_space_probe_t;

static task_address_space_probe_t task_as_probe_a;
static task_address_space_probe_t task_as_probe_b;

static void task_address_space_probe_worker(void *argument) {
    task_address_space_probe_t *probe = (task_address_space_probe_t *)argument;
    uint64_t i;
    for (i = 0u; i < CHOS_TASK_AS_STRESS_ITERATIONS; ++i) {
        uint64_t observed = *probe->alias;
        if (observed != probe->expected) ++probe->failures;
        *probe->alias = probe->expected;
        ++probe->iterations;
    }
}

static void run_task_address_space_test(void) {
    address_space_handle_t first = {0u, 0u, 0u};
    address_space_handle_t second = {0u, 0u, 0u};
    pmm_stats_t before_pmm, after_pmm;
    vmm_stats_t before_vmm, after_vmm;
    scheduler_stats_t before_sched, after_sched;
    address_space_info_t first_info, second_info;
    uint64_t first_page = 0u, second_page = 0u;
    volatile uint64_t *first_backing, *second_backing;
    uint32_t first_task_id = 0u, second_task_id = 0u;
    int first_created = 0, second_created = 0;
    int first_mapped = 0, second_mapped = 0;
    int task_a_created = 0, task_b_created = 0;
    int pin_check = 0, scheduler_ok = 0, ok = 0;
    uint64_t kernel_root = vmm_kernel_root_table_address();

    if (scheduler_is_running() || vmm_current_root_table_address() != kernel_root) {
        uart_puts("[FAIL] task-as-test requires an idle scheduler and the canonical kernel root\n");
        return;
    }
    vmm_get_stats(&before_vmm);
    if (!vmm_is_enabled() || before_vmm.dynamic_mappings != 0u) {
        uart_puts("[FAIL] MMU must be active and the dynamic alias window empty\n");
        return;
    }
    pmm_get_stats(&before_pmm);
    scheduler_get_stats(&before_sched);
    uart_puts("Testing two timer-preempted EL1 tasks with the same user VA mapped to separate physical pages.\n");

    if (address_space_create(&first) != AS_OK) goto cleanup;
    first_created = 1;
    if (address_space_create(&second) != AS_OK) goto cleanup;
    second_created = 1;
    if (address_space_alloc_user_page(first, &first_page) != AS_OK) goto cleanup;
    if (address_space_alloc_user_page(second, &second_page) != AS_OK) goto cleanup;
    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE, first_page, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    first_mapped = 1;
    if (address_space_map_user_page(second, CHOS_AS_USER_VA_BASE, second_page, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    second_mapped = 1;

    first_backing = (volatile uint64_t *)(uintptr_t)first_page;
    second_backing = (volatile uint64_t *)(uintptr_t)second_page;
    task_as_probe_a.alias = (volatile uint64_t *)(uintptr_t)CHOS_AS_USER_VA_BASE;
    task_as_probe_a.expected = 0x43484f5354415341ull;
    task_as_probe_a.iterations = 0u;
    task_as_probe_a.failures = 0u;
    task_as_probe_b.alias = (volatile uint64_t *)(uintptr_t)CHOS_AS_USER_VA_BASE;
    task_as_probe_b.expected = 0x43484f5354415342ull;
    task_as_probe_b.iterations = 0u;
    task_as_probe_b.failures = 0u;
    *first_backing = task_as_probe_a.expected;
    *second_backing = task_as_probe_b.expected;
    __asm__ volatile("dsb sy" ::: "memory");

    if (!scheduler_set_preemption_enabled(1)) goto cleanup;
    task_a_created = scheduler_create_task_in_address_space(
        "as-probe-A", task_address_space_probe_worker, &task_as_probe_a, first, &first_task_id);
    if (!task_a_created) goto run_partial_cleanup;
    task_b_created = scheduler_create_task_in_address_space(
        "as-probe-B", task_address_space_probe_worker, &task_as_probe_b, second, &second_task_id);
    if (!task_b_created) goto run_partial_cleanup;

    pin_check = address_space_destroy(first) == AS_ERR_REFERENCED &&
                address_space_destroy(second) == AS_ERR_REFERENCED;
    if (!pin_check) goto run_partial_cleanup;

    scheduler_ok = scheduler_run();
    scheduler_get_stats(&after_sched);
    if (!scheduler_ok) goto cleanup;
    if (!address_space_get_info(first, &first_info) || !address_space_get_info(second, &second_info)) goto cleanup;
    vmm_get_stats(&after_vmm);
    ok = task_as_probe_a.iterations == CHOS_TASK_AS_STRESS_ITERATIONS &&
         task_as_probe_b.iterations == CHOS_TASK_AS_STRESS_ITERATIONS &&
         task_as_probe_a.failures == 0u && task_as_probe_b.failures == 0u &&
         after_sched.preemption_switches > before_sched.preemption_switches &&
         after_sched.address_space_switches > before_sched.address_space_switches &&
         after_sched.address_space_switch_failures == before_sched.address_space_switch_failures &&
         after_sched.pending_reaps == 0u && after_sched.address_space_tasks == 0u &&
         first_info.task_references == 0u && second_info.task_references == 0u &&
         vmm_current_root_table_address() == kernel_root &&
         after_vmm.dynamic_mappings == before_vmm.dynamic_mappings;

    uart_puts("Task A iterations/errors: "); print_decimal((size_t)task_as_probe_a.iterations);
    uart_putc('/'); print_decimal((size_t)task_as_probe_a.failures); uart_puts("\n");
    uart_puts("Task B iterations/errors: "); print_decimal((size_t)task_as_probe_b.iterations);
    uart_putc('/'); print_decimal((size_t)task_as_probe_b.failures); uart_puts("\n");
    uart_puts("TTBR0 root switches: "); print_decimal((size_t)(after_sched.address_space_switches - before_sched.address_space_switches));
    uart_puts("; switch failures: "); print_decimal((size_t)after_sched.address_space_switch_failures); uart_puts("\n");
    goto cleanup;

run_partial_cleanup:
    /* If only one task was created, run it so its retained address-space reference
     * is released by normal deferred reaping before we destroy the temporary roots. */
    (void)scheduler_run();
    (void)scheduler_set_preemption_enabled(0);

cleanup:
    if (!scheduler_is_running() && vmm_current_root_table_address() != kernel_root) {
        (void)address_space_deactivate();
    }
    if (first_created && first_mapped && !scheduler_is_running() &&
        address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE, NULL) == AS_OK) first_mapped = 0;
    if (second_created && second_mapped && !scheduler_is_running() &&
        address_space_unmap_user_page(second, CHOS_AS_USER_VA_BASE, NULL) == AS_OK) second_mapped = 0;
    if (first_created && !first_mapped && !scheduler_is_running() && address_space_destroy(first) == AS_OK) first_created = 0;
    if (second_created && !second_mapped && !scheduler_is_running() && address_space_destroy(second) == AS_OK) second_created = 0;
    (void)first_task_id;
    (void)second_task_id;
    pmm_get_stats(&after_pmm);
    vmm_get_stats(&after_vmm);
    if (!scheduler_is_running() && vmm_current_root_table_address() == kernel_root &&
        !scheduler_preemption_enabled() && address_space_count() == 0u &&
        before_pmm.free_pages == after_pmm.free_pages &&
        before_pmm.allocated_pages == after_pmm.allocated_pages &&
        before_vmm.dynamic_mappings == after_vmm.dynamic_mappings && ok) {
        uart_puts("[ OK ] task-bound address spaces, same-VA isolation under timer preemption, pinned lifetime, deferred cleanup and PMM baseline passed\n");
    } else {
        uart_puts("[FAIL] task/address-space scheduling isolation, cleanup or memory counters failed\n");
    }
}

static void print_lock_info(void) {
    uart_puts("CHOS locking rules (single-core prototype)\n");
    uart_puts("Permitted nested order: kernel heap -> PMM; process address-space lock -> PMM.\n");
    uart_puts("PMM must never call the heap or address-space manager. UART/console lock is isolated; do not nest it with allocator locks.\n");
    uart_puts("Never yield, sleep, schedule, or wait for a timer while holding a spinlock.\n");
    uart_puts("VMM page-table changes use local-IRQ critical sections; prefer vmm_map_page_auto() over find-then-map.\n");
    uart_puts("Do not call VMM mapping functions while holding allocator or UART locks.\n");
    uart_puts("IRQ handlers must not allocate or print. IRQ-save locks protect current-core state from timer preemption.\n");
    uart_puts("This is not SMP support: per-CPU IRQ state, SMP boot, and a multicore locking audit are still required.\n");
}

static void print_device_tree(const fdt_info_t *tree) {
    size_t i;
    if (tree == NULL || !tree->valid) {
        uart_puts("Device tree unavailable.\n");
        if (tree != NULL) {
            uart_puts("Reason: ");
            uart_puts(fdt_error_string(tree->error));
            uart_puts("\n");
        }
        return;
    }

    uart_puts("Flattened Device Tree (FDT)\n");
    uart_puts("Platform: ");
    uart_puts(platform_name());
    uart_puts("\nBlob size: ");
    print_decimal(tree->total_size);
    uart_puts(" bytes\nModel: ");
    uart_puts(tree->model[0] != '\0' ? tree->model : "(not provided)");
    uart_puts("\nRoot compatible: ");
    uart_puts(tree->compatible[0] != '\0' ? tree->compatible : "(not provided)");
    uart_puts("\n");

    if (tree->stdout_path[0] != '\0') {
        uart_puts("stdout-path: ");
        uart_puts(tree->stdout_path);
        uart_puts("\n");
    }
    if (tree->bootargs[0] != '\0') {
        uart_puts("bootargs: ");
        uart_puts(tree->bootargs);
        uart_puts("\n");
    }

    uart_puts("Memory regions: ");
    print_decimal(tree->memory_count);
    uart_puts("\n");
    for (i = 0; i < tree->memory_count; ++i) {
        uart_puts("  base=");
        print_hex64(tree->memory[i].base);
        uart_puts(" size=");
        print_hex64(tree->memory[i].size);
        uart_puts(" bytes\n");
    }

    uart_puts("Static reserved regions: ");
    print_decimal(tree->reserved_count);
    uart_puts("\n");
    for (i = 0; i < tree->reserved_count; ++i) {
        uart_puts("  ");
        uart_puts(tree->reserved[i].source == 1u ? "memreserve" : "/reserved-memory");
        uart_puts(" base=");
        print_hex64(tree->reserved[i].base);
        uart_puts(" size=");
        print_hex64(tree->reserved[i].size);
        uart_puts(" bytes\n");
    }
    uart_puts("Dynamic reserved-memory requests without fixed addresses: ");
    print_decimal(tree->reserved_dynamic_count);
    uart_puts("\n");

    uart_puts("Devices with compatible + reg (captured): ");
    print_decimal(tree->device_count);
    uart_puts("\n");
    for (i = 0; i < tree->device_count; ++i) {
        const fdt_device_t *device = &tree->devices[i];
        uart_puts("  ");
        uart_puts(device->name);
        uart_puts(" | ");
        uart_puts(device->compatible);
        if (device->has_reg) {
            uart_puts(" | reg=");
            print_hex64(device->reg_base);
            uart_puts(" size=");
            print_hex64(device->reg_size);
        }
        uart_puts("\n");
    }
    if (tree->devices_truncated) {
        uart_puts("  (device list truncated; increase FDT_MAX_DEVICES to retain more)\n");
    }
    if (tree->memory_count == 0u) {
        uart_puts("WARNING: no RAM regions were found in the device tree.\n");
    }
}

static int parse_pid(const char *text, uint32_t *pid_out) {
    uint64_t value = 0u;
    size_t i = 0u;
    if (text == NULL || pid_out == NULL || text[0] == '\0') return 0;
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > UINT32_MAX) return 0;
        ++i;
    }
    if (i == 0u) return 0;
    while (text[i] == ' ' || text[i] == '\t') ++i;
    if (text[i] != '\0' || value == 0u) return 0;
    *pid_out = (uint32_t)value;
    return 1;
}

static void print_process_list(void) {
    size_t i;
    chos_process_info_t process;
    process_reap_pending();
    uart_puts("PID     STATE       EXIT    NAME\n");
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (!process_get_info_at(i, &process)) continue;
        print_decimal(process.pid); uart_puts("     ");
        uart_puts(process_state_name(process.state));
        uart_puts("   "); print_signed64(process.exit_status);
        uart_puts("   "); uart_puts(process.name); uart_puts("\n");
    }
}

static void run_process_status_command(const char *argument, int wait_for_exit) {
    uint32_t pid;
    chos_process_info_t process;
    if (!parse_pid(argument, &pid)) {
        uart_puts(wait_for_exit ? "Usage: wait PID\n" : "Usage: status PID\n");
        return;
    }
    if (wait_for_exit && (!shell_runtime_active || !scheduler_is_running())) {
        uart_puts("wait requires the scheduled runtime. Enter 'runtime', then wait from that shell.\n");
        return;
    }
    if (!process_get_info_by_pid(pid, &process)) {
        uart_puts("No process record for PID "); print_decimal(pid); uart_puts(".\n");
        return;
    }
    if (wait_for_exit && process.state != CHOS_PROCESS_EXITED && process.state != CHOS_PROCESS_FAILED) {
        int query;
        /* Shell-side wait shares the same state primitive as the syscall ABI;
         * yield keeps the target process making progress. */
        do {
            query = process_query_pid(pid, NULL, NULL);
            if (query > 0) scheduler_yield();
        } while (query > 0);
        if (query < 0 || !process_get_info_by_pid(pid, &process)) {
            uart_puts("PID "); print_decimal(pid); uart_puts(" is no longer in the process history.\n");
            return;
        }
    }
    uart_puts("PID "); print_decimal(process.pid); uart_puts(" "); uart_puts(process.name);
    uart_puts(" state="); uart_puts(process_state_name(process.state));
    uart_puts(" exit="); print_signed64(process.exit_status); uart_puts("\n");
}

static void print_process_info(void) {
    size_t i;
    chos_process_info_t process;
    chos_process_stats_t stats;
    chos_process_table_stats_t table;
    /* A task can finish just before the user enters procinfo. Drain deferred
     * scheduler/process cleanup before taking the diagnostic snapshot. */
    process_reap_pending();
    process_get_stats(&stats);
    process_get_table_stats(&table);
    uart_puts("CHOS process table (foreground and scheduled EL0 processes)\n");
    uart_puts("History records: "); print_decimal(table.history_records);
    uart_puts(" / "); print_decimal(CHOS_PROCESS_MAX); uart_puts("\n");
    uart_puts("Active: "); print_decimal(table.active_processes);
    uart_puts("; reusable terminal records: "); print_decimal(table.reusable_terminal_records);
    uart_puts("; pending cleanup: "); print_decimal(table.pending_cleanup); uart_puts("\n");
    uart_puts("Totals: started="); print_decimal(stats.started);
    uart_puts(" exited="); print_decimal(stats.exited);
    uart_puts(" failed="); print_decimal(stats.failed); uart_puts("\n");
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (!process_get_info_at(i, &process)) continue;
        uart_puts("  pid="); print_decimal(process.pid);
        uart_puts(" name="); uart_puts(process.name);
        uart_puts(" state="); uart_puts(process_state_name(process.state));
        uart_puts(" as="); print_decimal(process.address_space_id);
        uart_puts(" image-pages="); print_decimal(process.image_pages);
        uart_puts(" stack-pages="); print_decimal(process.stack_pages);
        uart_puts(" entry="); print_hex64(process.entry_address);
        uart_puts(" exit="); print_signed64(process.exit_status);
        uart_puts(" task="); print_decimal(process.scheduler_task_id);
        uart_puts(process.cleanup_pending ? " cleanup=pending\n" : " cleanup=complete\n");
        if (process.fault_esr != 0u) {
            uart_puts("    fault ESR="); print_hex64(process.fault_esr);
            uart_puts(" ELR="); print_hex64(process.fault_elr);
            uart_puts(" FAR="); print_hex64(process.fault_far); uart_puts("\n");
        }
    }
    uart_puts("Executable formats: CHCX v1 flat RX; v2 page-separated RX/R/RW with zero-filled BSS.\n");
    uart_puts("Loader source: read-only packaged ROMFS; disk-backed filesystem loading is not implemented yet.\n");
}

static void print_romfs_files(void) {
    size_t i;
    chos_romfs_file_t file;
    uart_puts("CHOS read-only ROMFS (embedded files; /BIN)\n");
    if (!romfs_validate_all()) {
        uart_puts("[FAIL] one or more packaged CHCX files failed validation\n");
        return;
    }
    for (i = 0u; i < romfs_file_count(); ++i) {
        if (romfs_get_at(i, &file)) {
            uart_puts("  "); uart_puts(file.path); uart_puts("  ");
            print_decimal(file.size); uart_puts(" bytes  [CHCX valid]\n");
        }
    }
}

static void print_spawn_result(const char *path, chos_process_error_t error, uint32_t pid) {
    uint64_t flags = uart_write_begin();
    if (error == PROCESS_OK) {
        uart_puts_locked("[SPAWNED] pid="); uart_put_u64_locked(pid);
        uart_puts_locked(" path="); uart_puts_locked(path);
        uart_puts_locked(shell_runtime_active ? " (running asynchronously)\n"
                                              : " (queued; use runqueue or runtime)\n");
    } else {
        uart_puts_locked("[FAIL] spawn path: ");
        uart_puts_locked(process_error_string(error));
        uart_puts_locked("\n");
    }
    uart_write_end(flags);
}

static void run_spawn_path_command(char *command) {
    uint32_t pid = 0u;
    chos_process_error_t error;
    char *path = skip_spaces(command);
    char *arguments = NULL;
    char *cursor;
    if (path == NULL || *path == '\0') {
        uart_puts("Usage: spawn /BIN/CDEMO.CHX [arguments...]\n");
        return;
    }
    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == ' ' || *cursor == '\t') {
            *cursor++ = '\0';
            arguments = skip_spaces(cursor);
            break;
        }
    }
    error = process_spawn_path_args(path, arguments, &pid);
    print_spawn_result(path, error, pid);
}

static void run_process_path_command(char *path) {
    chos_process_result_t result = {0};
    chos_process_error_t error;
    if (path == NULL || *path == '\0') {
        uart_puts("Usage: exec /BIN/HELLO.CHX\n");
        return;
    }
    if (shell_runtime_active || scheduler_is_running()) {
        run_spawn_path_command(path);
        return;
    }
    /* The synchronous boot-shell runner currently launches without user-supplied
     * arguments; use spawn from runtime for argc/argv-capable execution. */
    { char *cursor; for (cursor = path; *cursor != '\0'; ++cursor) if (*cursor == ' ' || *cursor == '\t') { *cursor = '\0'; break; } }
    error = process_run_path(path, &result);
    if (error == PROCESS_OK) {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[PROCESS] pid="); uart_put_u64_locked(result.pid);
        uart_puts_locked(" exit="); uart_put_u64_locked((uint64_t)result.exit_status);
        uart_puts_locked("\n");
        uart_write_end(flags);
    } else if (error == PROCESS_ERR_EXECUTION && result.completed) {
        uart_puts("[PROCESS] user program faulted and was isolated; inspect procinfo for ESR/ELR/FAR.\n");
    } else {
        uart_puts("[FAIL] exec: "); uart_puts(process_error_string(error)); uart_puts("\n");
    }
}

static void run_queued_tasks_command(void) {
    scheduler_stats_t before, after;
    if (scheduler_is_running()) {
        uart_puts("The scheduler is already active; queued tasks run asynchronously in this runtime.\n");
        return;
    }
    scheduler_get_stats(&before);
    if (before.pending_reaps != 0u) {
        process_reap_pending();
        scheduler_get_stats(&before);
    }
    if (scheduler_get_task_count() == 0u) {
        uart_puts("No queued scheduler tasks. Use 'spawn PATH' to queue a process.\n");
        return;
    }
    uart_puts("Dispatching queued tasks with timer preemption; this command returns when they finish.\n");
    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] scheduler could not dispatch queued tasks\n");
        return;
    }
    process_reap_pending();
    scheduler_get_stats(&after);
    uart_puts("[ OK ] queued task batch finished; newly completed tasks: ");
    print_decimal((size_t)(after.completed_tasks - before.completed_tasks));
    uart_puts("\n");
}

static void run_romfs_self_test_command(void) {
    if (process_romfs_self_test()) uart_puts("ROMFS self-test completed successfully.\n");
    else uart_puts("ROMFS self-test failed; inspect process diagnostics.\n");
}

static void run_process_demo_command(void) {
    chos_process_error_t error = process_run_demo();
    if (error == PROCESS_OK) {
        uart_puts("[ OK ] CHCX executable loaded, executed in EL0 and cleaned up\n");
    } else {
        uart_puts("[FAIL] process loader/runner: ");
        uart_puts(process_error_string(error));
        uart_puts("\n");
    }
}

static void run_process_self_test_command(void) {
    chos_process_error_t error = process_self_test();
    if (error == PROCESS_OK) {
        uart_puts("[ OK ] CHCX validation, EL0 SYS_WRITE/SYS_EXIT, rejected user pointer, and process cleanup passed\n");
    } else {
        uart_puts("[FAIL] process self-test: ");
        uart_puts(process_error_string(error));
        uart_puts("\n");
    }
}

static void command_netinfo(void) {
    chos_virtio_net_info_t info;
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    chos_net_device_get_info(&info);
    uart_puts("CHOS Ethernet status: ");uart_puts(chos_net_device_name());uart_puts("\n");
    uart_puts("Driver: "); uart_puts(info.ready ? "ready\n" : "unavailable\n");
    uart_puts("MAC: ");
    for (i = 0u; i < CHOS_NET_MAC_BYTES; ++i) {
        uart_putc(hex[(info.mac[i] >> 4) & 0x0fu]);
        uart_putc(hex[info.mac[i] & 0x0fu]);
        if (i + 1u < CHOS_NET_MAC_BYTES) uart_putc(':');
    }
    uart_puts("\nLink: ");
    uart_puts(!info.ready ? "unavailable\n" :
              (!info.link_status_known ? "unknown (device has no negotiated status feature)\n" :
               (info.link_up ? "up\n" : "down\n")));
    uart_puts("MMIO base: "); print_hex64((uint64_t)info.mmio_base); uart_puts("\n");
    uart_puts("Received frames: "); print_decimal((size_t)info.rx_frames);
    uart_puts("; dropped frames: "); print_decimal((size_t)info.rx_dropped); uart_puts("\n");
    uart_puts("Transmitted frames: "); print_decimal((size_t)info.tx_frames);
    uart_puts("; TX errors: "); print_decimal((size_t)info.tx_errors); uart_puts("\n");
    uart_puts("RX queue: size="); print_decimal((size_t)info.rx_queue_size);
    uart_puts(" configured, QueueNum readback="); print_decimal((size_t)info.rx_queue_num_readback);
    uart_puts(" ready="); print_decimal((size_t)info.rx_queue_ready);
    uart_puts(" avail_idx="); print_decimal((size_t)info.rx_available_idx);
    uart_puts(" used_idx="); print_decimal((size_t)info.rx_device_used_idx);
    uart_puts(" consumed_idx="); print_decimal((size_t)info.rx_consumed_idx);
    uart_puts(" IRQ status="); print_hex64((uint64_t)info.interrupt_status); uart_puts("\n");
    uart_puts("DHCP/IPv4 status: run 'netstatus'. DNS and ICMP diagnostics are available; bounded outbound TCP client is available; TLS/HTTPS and browser support are not available yet.\n");
}

static void print_ipv4(uint32_t address) {
    print_decimal((size_t)((address >> 24) & 0xffu)); uart_putc('.');
    print_decimal((size_t)((address >> 16) & 0xffu)); uart_putc('.');
    print_decimal((size_t)((address >> 8) & 0xffu)); uart_putc('.');
    print_decimal((size_t)(address & 0xffu));
}

static void command_netstatus(void) {
    chos_net_stack_info_t info;
    chos_net_dns_info_t dns;
    chos_net_ping_info_t ping;
    chos_net_stack_get_info(&info);
    chos_net_dns_get_info(&dns);
    chos_net_ping_get_info(&ping);
    uart_puts("CHOS network configuration\nDriver: ");
    uart_puts(info.driver_ready ? "ready\n" : "unavailable\n");
    uart_puts("DHCP: "); uart_puts(chos_net_dhcp_state_name(info.dhcp_state)); uart_puts("\nIPv4: ");
    if (info.ipv4_address != 0u) print_ipv4(info.ipv4_address);
    else uart_puts("not configured");
    uart_puts("\nMask: "); if (info.subnet_mask != 0u) print_ipv4(info.subnet_mask); else uart_puts("unset");
    uart_puts("\nGateway: "); if (info.gateway_address != 0u) print_ipv4(info.gateway_address); else uart_puts("unset");
    uart_puts("\nDNS: "); if (info.dns_address != 0u) print_ipv4(info.dns_address); else uart_puts("unset");
    uart_puts("\nDHCP server: "); if (info.dhcp_server != 0u) print_ipv4(info.dhcp_server); else uart_puts("unknown");
    uart_puts("\nLease (seconds): "); print_decimal((size_t)info.lease_seconds);
    uart_puts("; transmit attempts: "); print_decimal((size_t)info.retries);
    uart_puts("; replies accepted/rejected: "); print_decimal((size_t)info.rx_dhcp_replies);
    uart_putc('/'); print_decimal((size_t)info.rejected_dhcp_replies);
    uart_puts("; ARP requests/replies: "); print_decimal((size_t)info.arp_requests_sent);
    uart_putc('/'); print_decimal((size_t)info.arp_replies_received); uart_puts("\nDNS lookup: ");
    uart_puts(chos_net_dns_state_name(dns.state));
    if (dns.hostname[0] != '\0') { uart_puts(" for "); uart_puts(dns.hostname); }
    if (dns.state == CHOS_NET_DNS_RESOLVED) {
        uart_puts(" -> "); print_ipv4(dns.ipv4_address); uart_puts(" (TTL ");
        print_decimal((size_t)dns.ttl_seconds); uart_puts("s)");
    }
    uart_puts("\nPing: "); uart_puts(chos_net_ping_state_name(ping.state));
    if (ping.destination_ipv4 != 0u) { uart_puts(" to "); print_ipv4(ping.destination_ipv4); }
    if (ping.state == CHOS_NET_PING_REPLIED) {
        uart_puts(" in ");
        print_decimal((size_t)((ping.elapsed_ticks * 1000u) / CHOS_TIMER_HZ));
        uart_puts(" ms");
    }
    uart_puts("; attempts: "); print_decimal(ping.attempts); uart_puts("\n");
}

static int parse_ipv4_text(const char *text, uint32_t *address_out) {
    uint32_t address = 0u;
    unsigned part = 0u, value = 0u, digits = 0u;
    const char *p = text;
    if (text == NULL || address_out == NULL) return 0;
    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            value = value * 10u + (unsigned)(*p - '0');
            if (value > 255u || ++digits > 3u) return 0;
        } else if (*p == '.' && digits != 0u && part < 3u) {
            address = (address << 8) | value;
            ++part; value = 0u; digits = 0u;
        } else return 0;
        ++p;
    }
    if (part != 3u || digits == 0u) return 0;
    address = (address << 8) | value;
    *address_out = address;
    return address != 0u && address != 0xffffffffu;
}

static int parse_decimal_u16(const char *text, uint16_t *value_out) {
    uint32_t value = 0u;
    const char *p = text;
    if (text == NULL || value_out == NULL || *p == '\0') return 0;
    while (*p != '\0') {
        if (*p < '0' || *p > '9') return 0;
        value = value * 10u + (uint32_t)(*p - '0');
        if (value > 65535u) return 0;
        ++p;
    }
    if (value == 0u) return 0;
    *value_out = (uint16_t)value;
    return 1;
}

static int parse_tcp_handle(char *text, int *handle_out, char **tail_out) {
    uint32_t value = 0u;
    char *p = text;
    if (text == NULL || handle_out == NULL) return 0;
    p = skip_spaces(p);
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10u + (uint32_t)(*p - '0');
        if (value > CHOS_TCP_MAX_SOCKETS) return 0;
        ++p;
    }
    if (value == 0u) return 0;
    if (*p != '\0' && *p != ' ' && *p != '\t') return 0;
    if (tail_out != NULL) *tail_out = skip_spaces(p);
    else if (*skip_spaces(p) != '\0') return 0;
    *handle_out = (int)value;
    return 1;
}

static void command_tcp_connect(char *args) {
    chos_net_stack_info_t net;
    uint32_t remote_ip;
    uint16_t remote_port;
    char *p, *port_text;
    int handle;
    args = skip_spaces(args);
    p = args;
    while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
    if (*p == '\0') { uart_puts("Usage: tcpconnect IPv4-address port\n"); return; }
    *p++ = '\0';
    port_text = skip_spaces(p);
    p = port_text;
    while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
    if (*p != '\0') { *p++ = '\0'; if (*skip_spaces(p) != '\0') { uart_puts("Usage: tcpconnect IPv4-address port\n"); return; } }
    if (!parse_ipv4_text(args, &remote_ip) || !parse_decimal_u16(port_text, &remote_port)) {
        uart_puts("Usage: tcpconnect IPv4-address port\n"); return;
    }
    chos_net_stack_get_info(&net);
    if (!net.driver_ready || net.dhcp_state != CHOS_NET_DHCP_BOUND || net.ipv4_address == 0u) {
        uart_puts("TCP connect requires a bound DHCP/IPv4 configuration; run netstatus.\n"); return;
    }
    handle = chos_tcp_open(0u);
    if (handle < 0 || !chos_tcp_connect(handle, remote_ip, remote_port)) {
        if (handle > 0) (void)chos_tcp_close(handle);
        uart_puts("Could not allocate/start TCP connection.\n"); return;
    }
    uart_puts("TCP connection started; handle="); print_decimal((size_t)handle);
    uart_puts(". Run 'tcpstatus' until established or failed.\n");
}

static void command_tcp_status(void) {
    int handle;
    int found = 0;
    for (handle = 1; handle <= (int)CHOS_TCP_MAX_SOCKETS; ++handle) {
        chos_tcp_socket_info_t info;
        if (!chos_tcp_get_info(handle, &info)) continue;
        found = 1;
        uart_puts("Socket "); print_decimal((size_t)handle); uart_puts(": ");
        uart_puts(chos_tcp_state_name(info.state)); uart_puts(" local="); print_decimal(info.local_port);
        uart_puts(" remote=");
        if (info.remote_ipv4 != 0u) print_ipv4(info.remote_ipv4); else uart_puts("unset");
        uart_putc(':'); print_decimal(info.remote_port);
        uart_puts(" error="); uart_puts(chos_tcp_error_name(info.error));
        uart_puts(" retransmissions="); print_decimal(info.retransmissions);
        uart_puts(" rx="); print_decimal(info.receive_available); uart_puts(" bytes, tx=");
        print_decimal((size_t)info.bytes_sent); uart_puts(" bytes\n");
    }
    if (!found) uart_puts("No TCP sockets allocated. Use tcpconnect IPv4-address port.\n");
}

static void command_tcp_send(char *args) {
    uint8_t decoded[LINE_CAPACITY];
    char *text;
    size_t src = 0u, dst = 0u;
    int handle, sent;
    if (!parse_tcp_handle(args, &handle, &text) || text == NULL || *text == '\0') {
        uart_puts("Usage: tcpsend HANDLE text (use \\\\r / \\\\n for line endings)\n"); return;
    }
    while (text[src] != '\0' && dst < sizeof(decoded)) {
        if (text[src] == '\\' && text[src + 1u] != '\0') {
            char esc = text[src + 1u];
            if (esc == 'r') { decoded[dst++] = '\r'; src += 2u; continue; }
            if (esc == 'n') { decoded[dst++] = '\n'; src += 2u; continue; }
            if (esc == 't') { decoded[dst++] = '\t'; src += 2u; continue; }
            if (esc == '\\') { decoded[dst++] = '\\'; src += 2u; continue; }
        }
        decoded[dst++] = (uint8_t)text[src++];
    }
    if (text[src] != '\0') { uart_puts("Text is too long.\n"); return; }
    sent = chos_tcp_send(handle, decoded, dst);
    if (sent > 0) { uart_puts("Queued "); print_decimal((size_t)sent); uart_puts(" TCP payload bytes.\n"); }
    else if (sent == 0) {
        chos_tcp_socket_info_t info;
        if (chos_tcp_get_info(handle, &info)) {
            if (info.state == CHOS_TCP_CLOSE_WAIT)
                uart_puts("Send deferred: peer sent FIN; CLOSE-WAIT permits sending, but a segment may be pending or the remote window may be zero. Check tcpstatus.\n");
            else if (info.state == CHOS_TCP_ESTABLISHED)
                uart_puts("Send deferred: an earlier segment may be unacknowledged or the remote window may be zero. Check tcpstatus.\n");
            else {
                uart_puts("Socket state is "); uart_puts(chos_tcp_state_name(info.state));
                uart_puts("; data can be sent only in established or close-wait.\n");
            }
        } else uart_puts("TCP send deferred; inspect tcpstatus.\n");
    } else uart_puts("Invalid TCP socket handle.\n");
}

static void command_tcp_recv(char *args) {
    uint8_t received[CHOS_TCP_MSS];
    size_t i;
    int handle, count;
    if (!parse_tcp_handle(args, &handle, NULL)) { uart_puts("Usage: tcprecv HANDLE\n"); return; }
    count = chos_tcp_recv(handle, received, sizeof(received));
    if (count < 0) { uart_puts("Invalid TCP socket handle.\n"); return; }
    if (count == 0) { uart_puts("No TCP data available yet.\n"); return; }
    uart_puts("Received "); print_decimal((size_t)count); uart_puts(" bytes:\n");
    for (i = 0u; i < (size_t)count; ++i) {
        uint8_t ch = received[i];
        if (ch == '\r' || ch == '\n' || ch == '\t' || (ch >= 0x20u && ch <= 0x7eu)) uart_putc((char)ch);
        else uart_putc('.');
    }
    uart_puts("\n");
}

static void command_tcp_close(char *args) {
    int handle, result;
    if (!parse_tcp_handle(args, &handle, NULL)) { uart_puts("Usage: tcpclose HANDLE\n"); return; }
    result = chos_tcp_close(handle);
    if (result > 0) uart_puts("TCP close accepted. The state may remain time-wait briefly.\n");
    else if (result == 0) uart_puts("TCP socket is busy; retry after pending data is acknowledged.\n");
    else uart_puts("Invalid TCP socket handle.\n");
}

static void command_diskinfo(void) {
    chos_block_device_t *device = storage_device();
    chos_chfs_stats_t stats;
    if (device == NULL) {
        uart_puts("Block device: unavailable (ROMFS-only recovery mode)\nReason: ");
        uart_puts(storage_error());
        uart_puts("\n");
        return;
    }
    uart_puts("Block device: "); uart_puts(device->name); uart_puts("\nVirtIO-MMIO base: ");
    print_hex64(virtio_blk_detected_base());
    uart_puts("\nSector size: 512 bytes\nSectors: ");
    print_decimal((size_t)device->sector_count); uart_puts("\nCapacity: ");
    print_decimal((size_t)((device->sector_count * 512u) / (1024u * 1024u))); uart_puts(" MiB\nCHFS: ");
    if (!storage_mounted()) { uart_puts("not mounted; use 'mount' or 'mkfs' on a new disk\n"); return; }
    uart_puts("mounted; current CHFS metadata validated\n"); storage_get_stats(&stats);
    uart_puts("Files: "); print_decimal(stats.file_count); uart_puts("; directories: ");
    print_decimal(stats.directory_count); uart_puts("; total entries: ");
    print_decimal(stats.file_count + stats.directory_count); uart_puts("/511; generation: ");
    print_decimal(stats.generation); uart_puts("\nData sectors used: ");
    print_decimal(stats.used_sectors); uart_puts("; approximate free sectors: "); print_decimal(stats.free_sectors); uart_puts("\n");
}

static void command_fsls(void) {
    size_t i = 0u; chos_chfs_entry_t entry;
    if (!storage_mounted()) { uart_puts("CHFS is not mounted. Use 'mount' or 'mkfs'.\n"); return; }
    uart_puts("Path                                             Bytes  Sectors\n");
    while (storage_get_at(i, &entry)) {
        uart_puts(entry.path);
        { size_t j = 0u, n = 0u; while (entry.path[j] != '\0') { ++j; ++n; }
          while (n++ < 48u) uart_putc(' '); }
        print_decimal(entry.size_bytes); uart_puts("  "); print_decimal(entry.sector_count); uart_puts("\n"); ++i;
    }
    if (i == 0u) uart_puts("(empty)\n");
}

static void command_fswrite(char *args) {
    char path[CHOS_CHFS_PATH_MAX]; size_t n = 0u, length = 0u; char *payload;
    if (!storage_mounted()) { uart_puts("CHFS is not mounted.\n"); return; }
    args = skip_spaces(args);
    while (args[n] != '\0' && args[n] != ' ' && args[n] != '\t') {
        if (n + 1u >= sizeof(path)) { uart_puts("Path too long.\n"); return; }
        path[n] = args[n]; ++n;
    }
    path[n] = '\0'; payload = skip_spaces(args + n);
    while (payload[length] != '\0') ++length;
    if (n == 0u || length == 0u) { uart_puts("Usage: fswrite /PATH text to store\n"); return; }
    if (storage_write_file(path, payload, length)) uart_puts("[ OK ] file written (directory committed to alternate metadata copy)\n");
    else uart_puts("[FAIL] write failed: check path, free contiguous space, or directory capacity\n");
}

static void command_fsread(const char *path) {
    chos_chfs_entry_t entry; void *buffer; size_t got = 0u, i; int text_file = 1;
    if (!storage_mounted()) { uart_puts("CHFS is not mounted.\n"); return; }
    if (!storage_lookup(path, &entry)) { uart_puts("File not found.\n"); return; }
    buffer = kheap_alloc(entry.size_bytes);
    if (buffer == NULL) { uart_puts("Not enough kernel memory to read this file.\n"); return; }
    if (!storage_read_file(path, buffer, entry.size_bytes, &got)) { uart_puts("[FAIL] file checksum or block read failed\n"); (void)kheap_free(buffer); return; }
    for (i = 0u; i < got; ++i) {
        unsigned char ch = ((unsigned char *)buffer)[i];
        if (ch == 0u || (ch < 0x20u && ch != '\n' && ch != '\r' && ch != '\t')) { text_file = 0; break; }
    }
    if (!text_file) {
        uart_puts("Binary file: "); print_decimal(got); uart_puts(" bytes; not displayed as text.\n");
    } else {
        for (i = 0u; i < got; ++i) uart_putc(((const char *)buffer)[i]);
        if (got == 0u || ((const char *)buffer)[got - 1u] != '\n') uart_puts("\n");
    }
    (void)kheap_free(buffer);
}

static void command_fsrm(const char *path) {
    if (!storage_mounted()) { uart_puts("CHFS is not mounted.\n"); return; }
    if (storage_remove_file(path)) uart_puts("[ OK ] file removed\n"); else uart_puts("[FAIL] file not found or metadata update failed\n");
}

static void print_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help       Show this help\n");
    uart_puts("  ver        Show the CHOS version\n");
    uart_puts("  sysinfo    Show basic system information\n");
    uart_puts("  devtree    Show parsed device tree, reservations and devices\n");
    uart_puts("  meminfo    Show physical page allocator status\n");
    uart_puts("  vmminfo    Show MMU state and bootstrap mappings\n");
    uart_puts("  asinfo     Show prepared process address spaces and user VA policy\n");
    uart_puts("  as-test    Test private user roots, mappings, ownership and teardown\n");
    uart_puts("  as-switch-test  Switch TTBR0 between two EL1-only roots and verify same-VA isolation\n");
    uart_puts("  task-as-test Test timer-preempted tasks bound to private address spaces\n");
    uart_puts("  vmm-test   Test MMU permissions, dynamic map/unmap and targeted TLB invalidation\n");
    uart_puts("  vmm-preempt-test Stress atomic dynamic VA mapping under timer preemption\n");
    uart_puts("  pmm-test   Allocate and free one page as a self-test\n");
    uart_puts("  heapinfo   Show kernel heap usage\n");
    uart_puts("  heap-test  Test allocation, alignment, reuse and coalescing\n");
    uart_puts("  timerinfo  Show generic timer and interrupt counters\n");
    uart_puts("  irqinfo    Show full IRQ frame diagnostics for preemption preparation\n");
    uart_puts("  tick-test  Verify that timer IRQ ticks advance\n");
    uart_puts("  taskinfo   Show scheduler, deferred cleanup and task states\n");
    uart_puts("  sched-test Run two yielding demo tasks (cooperative only)\n");
    uart_puts("  sched-sleep-test Test task sleep, timer wakeups and idle waiting\n");
    uart_puts("  preempt-test Run CPU-bound tasks with opt-in timer preemption\n");
    uart_puts("  preempt-sleep-test Test preemptive sleep, idle frames and wakeups\n");
    uart_puts("  register-stress-test Check x0-x30 and SP across timer preemption\n");
    uart_puts("  console-stress-test Check atomic UART lines with concurrent task writers\n");
    uart_puts("  task-reuse-test  Stress task exit, reaping, and stack-slot reuse\n");
    uart_puts("  heap-preempt-test Stress heap alloc/free while timer-preempted tasks run\n");
    uart_puts("  lockinfo   Show permitted kernel lock nesting and context rules\n");
    uart_puts("  el0-test   Run the original direct EL0 syscall smoke test\n");
    uart_puts("  exec-demo  Run /BIN/HELLO.CHX through the common ROMFS loader\n");
    uart_puts("  process-test Validate CHCX, run demo, and verify complete process cleanup\n");
    uart_puts("  process-preempt-test Run two EL0 tasks; isolate a user fault under timer preemption\n");
    uart_puts("  process-reuse-test Run more EL0 exits than process slots to verify safe record recycling\n");
    uart_puts("  procinfo   Show process records, task IDs, fault registers and process counters\n");
    uart_puts("  proclist   List process IDs, names, states and exit statuses\n");
    uart_puts("  status PID Query a process by PID\n");
    uart_puts("  wait PID   Wait for a scheduled process and return its exit status\n");
    uart_puts("  apps       List packaged read-only CHCX programs\n");
    uart_puts("  diskinfo   Show VirtIO block and CHFS status\n");
    uart_puts("  netinfo    Show VirtIO Ethernet link, MAC and frame counters\n  netstatus  Show DHCP/IPv4, ARP, DNS and ping state\n  dnslookup  Start async DNS A lookup: dnslookup example.com\n  ping       Send ICMP echo: ping 192.168.1.1\n  tcpconnect Connect to IPv4 TCP service: tcpconnect 93.184.216.34 80\n  tcpstatus  Show outbound TCP socket states\n  tcpsend    Send text; use \\r and \\n for line endings\n  tcprecv    Read received TCP data: tcprecv HANDLE\n  tcpclose   Close TCP socket: tcpclose HANDLE\n");
    uart_puts("  mount      Mount the persistent CHFS volume\n");
    uart_puts("  mkfs       FORMAT the attached disk as CHFS (destructive)\n");
    uart_puts("  fsls       List files on persistent storage\n");
    uart_puts("  fsread P   Print a text file from storage\n");
    uart_puts("  rng-test   Read 32 bytes from the actual entropy queue (no byte dump)\n");
    uart_puts("  fswrite P  Store the rest of the command line as text\n");
    uart_puts("  fsrm P     Remove a storage file\n");
    uart_puts("  exec PATH  Run a packaged CHCX file in foreground; async in runtime mode\n");
    uart_puts("  spawn PATH Queue a CHCX process and return a PID immediately\n");
    uart_puts("  runqueue   Run queued processes and return when the batch exits\n");
    uart_puts("  runtime    Enter interactive preemptive runtime; spawn processes in background\n");
    uart_puts("  runtime-exit Leave runtime after scheduled processes finish\n");
    uart_puts("  async-test Test GETPID/WRITE/YIELD/SLEEP/EXIT syscalls and memory cleanup\n");
    uart_puts("  romfs-test Validate and run packaged programs; includes an isolated user-fault test\n");
    uart_puts("  echo TEXT  Print TEXT\n");
    uart_puts("  clear      Clear the terminal display\n");
    uart_puts("  gui        Show the desktop or report graphics status\n");
    uart_puts("  exception-test  Trigger a test BRK exception and panic\n");
    uart_puts("  halt       Stop the CPU (reset QEMU to continue)\n");
}

static void execute_command(char *line, const fdt_info_t *tree) {
    char *arg;

    line = skip_spaces(line);
    if (*line == '\0') return;

    if (strings_equal(line, "help")) {
        print_help();
    } else if (strings_equal(line, "ver")) {
        uart_puts("CHOS ARM64 kernel " CHOS_VERSION "\n");
    } else if (strings_equal(line, "sysinfo")) {
        uart_puts("System: CHOS (experimental)\n");
        uart_puts("Architecture: AArch64 / ARM64\n");
        uart_puts("Platform profile: ");
        uart_puts(platform_name());
        uart_puts("\nKernel mode: EL1; exception vectors installed\n");
        uart_puts("MMU: ");
        uart_puts(vmm_is_enabled() ? "enabled (fine-grained 4 KiB identity map)\n" : "not enabled by CHOS\n");
        uart_puts("Process address spaces: TTBR0 switching enabled; CHCX scheduled EL0 tasks available\n");
        uart_puts("Interrupt controller: ");
        uart_puts(interrupt_controller_ready() ? "GICv2 initialized\n" : "unavailable\n");
        uart_puts("ARM Generic Timer: ");
        uart_puts(timer_is_ready() ? "running at 100 Hz\n" : "unavailable\n");
        uart_puts("CPU IRQ mask: ");
        uart_puts(timer_is_ready() ? "enabled\n" : "masked\n");
        uart_puts("Input/output: polled PL011 serial\n");
        {
            scheduler_stats_t scheduler_stats;
            scheduler_get_stats(&scheduler_stats);
            uart_puts("Task scheduling: ");
            uart_puts(scheduler_stats.preemption_enabled ? "timer preemption requested (development only)\n" : "cooperative by default; use preempt-test for opt-in timer preemption\n");
        }
        uart_puts("Device tree: ");
        uart_puts((tree != NULL && tree->valid) ? "parsed\n" : "unavailable\n");
        if (tree != NULL && tree->valid) {
            pmm_stats_t memory_stats;
            uart_puts("Discovered RAM regions: ");
            print_decimal(tree->memory_count);
            uart_puts("; retained device records: ");
            print_decimal(tree->device_count);
            uart_puts("\nFDT static reserved regions: ");
            print_decimal(tree->reserved_count);
            uart_puts("; dynamic reservations: ");
            print_decimal(tree->reserved_dynamic_count);
            uart_puts("\nPhysical page allocator: ");
            pmm_get_stats(&memory_stats);
            uart_puts(memory_stats.initialized ? "initialized\n" : "unavailable\n");
            {
                kheap_stats_t heap_stats;
                kheap_get_stats(&heap_stats);
                uart_puts("Kernel heap: ");
                uart_puts(heap_stats.initialized ? "initialized\n" : "unavailable\n");
            }
            if (tree->model[0] != '\0') {
                uart_puts("Hardware model: ");
                uart_puts(tree->model);
                uart_puts("\n");
            }
        }
    } else if (strings_equal(line, "devtree")) {
        print_device_tree(tree);
    } else if (strings_equal(line, "timerinfo")) {
        uint64_t ticks = timer_ticks();
        uart_puts("ARM Generic Timer and interrupt diagnostics\n");
        uart_puts("GICv2 controller: ");
        uart_puts(interrupt_controller_ready() ? "ready\n" : "unavailable\n");
        uart_puts("Timer: ");
        uart_puts(timer_is_ready() ? "enabled\n" : "unavailable\n");
        uart_puts("Counter frequency: ");
        print_decimal((size_t)timer_frequency_hz());
        uart_puts(" Hz\nTimer ticks: ");
        print_decimal((size_t)ticks);
        uart_puts("\nEstimated uptime: ");
        print_decimal((size_t)((ticks * 1000u) / CHOS_TIMER_HZ));
        uart_puts(" ms\nAcknowledged IRQs: ");
        print_decimal((size_t)interrupt_count());
        uart_puts("\nUnexpected IRQs: ");
        print_decimal((size_t)interrupt_unhandled_count());
        uart_puts("\nSpurious IAR responses (timer is quarantined on first): ");
        print_decimal((size_t)interrupt_spurious_count());
        uart_puts("\n");
    } else if (strings_equal(line, "irqinfo")) {
        uart_puts("AArch64 IRQ context diagnostics\n");
        uart_puts("Frame size: 288 bytes (x0-x30, ELR_EL1, SPSR_EL1, ESR_EL1, SP_EL0)\n");
        uart_puts("Frames captured: "); print_decimal((size_t)interrupt_context_frame_count()); uart_puts("\n");
        uart_puts("Last interrupted PC (ELR_EL1): "); print_hex64(interrupt_context_last_elr()); uart_puts("\n");
        uart_puts("Last saved PSTATE (SPSR_EL1): "); print_hex64(interrupt_context_last_spsr()); uart_puts("\n");
        uart_puts("Dispatcher policy: returns interrupted context unchanged\n");
        uart_puts("IRQ-time scheduler mutation: disabled; timer handler updates tick only\n");
        uart_puts("Preemptive task switching: not enabled in 0.5.0\n");
    } else if (strings_equal(line, "tick-test")) {
        uint64_t start_ticks = timer_ticks();
        uint64_t start_counter = timer_counter();
        uint64_t frequency = timer_frequency_hz();
        if (!timer_is_ready() || frequency == 0u) {
            uart_puts("[FAIL] Generic timer is not available\n");
        } else {
            uart_puts("Waiting for 20 timer ticks (expected about 200 ms)...\n");
            while ((timer_ticks() - start_ticks) < 20u &&
                   (timer_counter() - start_counter) < (frequency * 2u)) {
                __asm__ volatile("yield" ::: "memory");
            }
            if ((timer_ticks() - start_ticks) >= 20u) {
                uart_puts("[ OK ] timer IRQ ticks advanced by at least 20\n");
            } else {
                uart_puts("[FAIL] timer IRQ did not advance before timeout\n");
            }
        }
    } else if (strings_equal(line, "meminfo")) {
        pmm_stats_t stats;
        pmm_get_stats(&stats);
        uart_puts("Physical memory manager (PMM)\n");
        uart_puts("Page size: ");
        print_decimal(CHOS_PAGE_SIZE);
        uart_puts(" bytes\nMMU: ");
        uart_puts(vmm_is_enabled() ? "enabled; bootstrap map is identity-mapped\n" : "not enabled by CHOS; addresses remain physical/identity\n");
        if (stats.initialized) {
            uart_puts("Status: initialized\nRAM regions managed: ");
            print_decimal(stats.region_count);
            uart_puts("\nPages tracked: ");
            print_decimal((size_t)stats.total_pages);
            uart_puts(" (approximately ");
            print_decimal((size_t)((stats.total_pages * CHOS_PAGE_SIZE) / (1024u * 1024u)));
            uart_puts(" MiB)\nReserved pages (kernel + DTB): ");
            print_decimal((size_t)stats.reserved_pages);
            uart_puts("\nFree pages: ");
            print_decimal((size_t)stats.free_pages);
            uart_puts(" (approximately ");
            print_decimal((size_t)((stats.free_pages * CHOS_PAGE_SIZE) / (1024u * 1024u)));
            uart_puts(" MiB)\nAllocated pages: ");
            print_decimal((size_t)stats.allocated_pages);
            uart_puts("\n");
        } else {
            uart_puts("Status: unavailable\nReason: ");
            uart_puts(pmm_error_string(stats.error));
            uart_puts("\n");
        }
        uart_puts("Bootstrap stack: 65536 bytes (included in kernel reservation)\n");
        if (tree != NULL && tree->valid && tree->memory_count > 0u) {
            size_t i;
            uart_puts("FDT RAM regions:\n");
            for (i = 0; i < tree->memory_count; ++i) {
                uart_puts("  base=");
                print_hex64(tree->memory[i].base);
                uart_puts(" size=");
                print_hex64(tree->memory[i].size);
                uart_puts(" bytes\n");
            }
            uart_puts("FDT static reservations retained: ");
            print_decimal(tree->reserved_count);
            uart_puts("; dynamic reservations: ");
            print_decimal(tree->reserved_dynamic_count);
            uart_puts("\n");
        } else {
            uart_puts("RAM size: unknown (device tree unavailable)\n");
        }
    } else if (strings_equal(line, "vmminfo")) {
        vmm_stats_t stats;
        vmm_get_stats(&stats);
        uart_puts("CHOS virtual memory bootstrap\n");
        uart_puts("MMU state: ");
        uart_puts(stats.enabled ? "enabled by CHOS\n" : "not enabled by CHOS\n");
        uart_puts("Translation tables prepared: ");
        uart_puts(stats.prepared ? "yes\n" : "no\n");
        uart_puts("Root table address: "); print_hex64(stats.root_table_address); uart_puts("\n");
        uart_puts("Granule: "); print_decimal(stats.granule_bytes); uart_puts(" bytes; VA size: ");
        print_decimal(stats.va_bits); uart_puts(" bits\n");
        uart_puts("L1 tables: "); print_decimal(stats.l1_table_count);
        uart_puts("; L3 tables used: "); print_decimal(stats.l3_tables_used); uart_puts("\n");
        uart_puts("Mapped RAM: "); print_decimal(stats.mapped_ram_pages); uart_puts(" pages (~");
        print_decimal((size_t)((stats.normal_map_bytes + (512u * 1024u)) / (1024u * 1024u))); uart_puts(" MiB)\n");
        uart_puts("Mapped device: "); print_decimal(stats.mapped_device_pages); uart_puts(" pages (~");
        print_decimal((size_t)(stats.device_map_bytes / 1024u)); uart_puts(" KiB)\n");
        uart_puts("Bootstrap stack guard (unmapped): "); print_hex64(stats.guard_page_address); uart_puts("\n");
        uart_puts("Per-task unmapped stack guards: "); print_decimal(stats.task_stack_guard_pages); uart_puts("\n");
        uart_puts("Permissions: .text RO+X, .rodata RO+XN, data/bss RW+XN; WXN enabled\n");
        uart_puts("Unmapped holes: low MMIO gaps, RAM beyond discovered regions, bootstrap + task stack guards\n");
        uart_puts("Dynamic VA window: "); print_hex64(VMM_DYNAMIC_VA_BASE); uart_puts(" to (exclusive) ");
        print_hex64(VMM_DYNAMIC_VA_LIMIT); uart_puts("; active aliases: ");
        print_decimal(stats.dynamic_mappings); uart_puts("\n");
        uart_puts("Map API: 4 KiB writable+XN aliases of mapped pages; auto-map atomically finds+installs a VA.\n");
        uart_puts("VMM state is protected from local timer preemption; SMP mapping is unsupported.\n");
        uart_puts("Platform profile: QEMU virt-specific; EL0 is isolated for foreground process runs.\n");
    } else if (strings_equal(line, "vmm-preempt-test")) {
        run_vmm_preempt_test();
    } else if (strings_equal(line, "asinfo")) {
        print_address_space_info();
    } else if (strings_equal(line, "as-test")) {
        run_address_space_test();
    } else if (strings_equal(line, "as-switch-test")) {
        run_address_space_switch_test();
    } else if (strings_equal(line, "task-as-test")) {
        run_task_address_space_test();
    } else if (strings_equal(line, "vmm-test")) {
        if (vmm_self_test()) {
            uart_puts("[ OK ] MMU active; permissions, bootstrap/task guard pages, dynamic map/unmap and RAM alias probes passed\n");
        } else {
            uart_puts("[FAIL] VMM self-test failed or CHOS MMU map is not active\n");
        }
    } else if (strings_equal(line, "heapinfo")) {
        kheap_stats_t stats;
        kheap_get_stats(&stats);
        uart_puts("Kernel heap\n");
        if (!stats.initialized) {
            uart_puts("Status: unavailable (physical allocator not initialized)\n");
        } else {
            uart_puts("Arena count: "); print_decimal(stats.arena_count); uart_puts("\n");
            uart_puts("Pages backing heap: "); print_decimal((size_t)stats.total_pages); uart_puts("\n");
            uart_puts("Total arena bytes: "); print_decimal((size_t)stats.total_bytes); uart_puts("\n");
            uart_puts("Allocated block payload: "); print_decimal((size_t)stats.allocated_payload_bytes); uart_puts(" bytes\n");
            uart_puts("Free block payload: "); print_decimal((size_t)stats.free_payload_bytes); uart_puts(" bytes\n");
            uart_puts("Allocated blocks: "); print_decimal(stats.allocated_blocks); uart_puts("; free blocks: "); print_decimal(stats.free_blocks); uart_puts("\n");
            uart_puts("Alignment: 16 bytes; arena pages are retained for reuse\n");
        }
    } else if (strings_equal(line, "heap-test")) {
        kheap_stats_t before, after;
        uint8_t *a;
        uint8_t *b;
        uint8_t *c;
        size_t i;
        kheap_get_stats(&before);
        a = (uint8_t *)kheap_alloc(37u);
        b = (uint8_t *)kheap_alloc(700u);
        c = (uint8_t *)kheap_alloc(20000u);
        if (a == NULL || b == NULL || c == NULL) {
            if (a != NULL) (void)kheap_free(a);
            if (b != NULL) (void)kheap_free(b);
            if (c != NULL) (void)kheap_free(c);
            uart_puts("[FAIL] heap-test allocation failed\n");
        } else {
            int aligned = (((uintptr_t)a & (KHEAP_ALIGNMENT - 1u)) == 0u) &&
                          (((uintptr_t)b & (KHEAP_ALIGNMENT - 1u)) == 0u) &&
                          (((uintptr_t)c & (KHEAP_ALIGNMENT - 1u)) == 0u);
            int payload_ok = 1;
            int freed_b, freed_a, freed_c;
            for (i = 0u; i < 37u; ++i) a[i] = (uint8_t)(i ^ 0x5au);
            for (i = 0u; i < 700u; ++i) b[i] = (uint8_t)(i ^ 0xa5u);
            for (i = 0u; i < 20000u; ++i) c[i] = (uint8_t)(i ^ 0x3cu);
            for (i = 0u; i < 37u; ++i) if (a[i] != (uint8_t)(i ^ 0x5au)) payload_ok = 0;
            for (i = 0u; i < 700u; ++i) if (b[i] != (uint8_t)(i ^ 0xa5u)) payload_ok = 0;
            for (i = 0u; i < 20000u; ++i) if (c[i] != (uint8_t)(i ^ 0x3cu)) payload_ok = 0;
            if (!aligned || !payload_ok) {
                (void)kheap_free(b);
                (void)kheap_free(a);
                (void)kheap_free(c);
                uart_puts("[FAIL] heap-test alignment or payload check failed\n");
            } else {
                freed_b = kheap_free(b);
                freed_a = kheap_free(a);
                freed_c = kheap_free(c);
                if (!freed_b || !freed_a || !freed_c) {
                    uart_puts("[FAIL] heap-test free/coalescing path failed\n");
                } else {
                    kheap_get_stats(&after);
                    if (after.allocated_blocks == before.allocated_blocks &&
                        after.allocated_payload_bytes == before.allocated_payload_bytes &&
                        !kheap_free(a)) {
                        uart_puts("[ OK ] allocations aligned; payload preserved; free/coalescing and double-free checks passed\n");
                    } else {
                        uart_puts("[FAIL] heap counters did not return to baseline or double-free was accepted\n");
                    }
                }
            }
        }
    } else if (strings_equal(line, "pmm-test")) {
        pmm_stats_t before, after;
        uint64_t page_address = 0u;
        pmm_get_stats(&before);
        if (!before.initialized) {
            uart_puts("PMM self-test failed: allocator is not initialized.\n");
        } else if (!pmm_alloc_page(&page_address)) {
            uart_puts("PMM self-test failed: no free page available.\n");
        } else {
            uart_puts("Allocated page at ");
            print_hex64(page_address);
            uart_puts("\n");
            if (!pmm_free_page(page_address)) {
                uart_puts("PMM self-test failed: allocated page could not be freed.\n");
            } else {
                pmm_get_stats(&after);
                if (after.free_pages == before.free_pages &&
                    after.allocated_pages == before.allocated_pages) {
                    uart_puts("[ OK ] allocate/free round trip passed\n");
                } else {
                    uart_puts("PMM self-test failed: page counters did not return to baseline.\n");
                }
            }
        }
    } else if (strings_equal(line, "taskinfo")) {
        print_scheduler_info();
    } else if (strings_equal(line, "sched-test")) {
        run_scheduler_test();
    } else if (strings_equal(line, "sched-sleep-test")) {
        run_scheduler_sleep_test();
    } else if (strings_equal(line, "preempt-test")) {
        run_preemptive_test();
    } else if (strings_equal(line, "preempt-sleep-test")) {
        run_preemptive_sleep_test();
    } else if (strings_equal(line, "register-stress-test")) {
        run_register_preservation_test();
    } else if (strings_equal(line, "console-stress-test")) {
        run_console_stress_test();
    } else if (strings_equal(line, "task-reuse-test")) {
        run_task_reuse_test();
    } else if (strings_equal(line, "heap-preempt-test")) {
        run_heap_preempt_test();
    } else if (strings_equal(line, "lockinfo")) {
        print_lock_info();
    } else if (strings_equal(line, "procinfo")) {
        print_process_info();
    } else if (strings_equal(line, "proclist")) {
        print_process_list();
    } else if (starts_with(line, "status ")) {
        run_process_status_command(skip_spaces(line + 7), 0);
    } else if (starts_with(line, "wait ")) {
        run_process_status_command(skip_spaces(line + 5), 1);
    } else if (strings_equal(line, "diskinfo")) {
        command_diskinfo();
    } else if (strings_equal(line, "netinfo")) {
        command_netinfo();
    } else if (strings_equal(line, "netstatus")) {
        command_netstatus();
    } else if (starts_with(line, "dnslookup ")) {
        char *hostname = skip_spaces(line + 10u);
        if (*hostname == '\0') {
            uart_puts("Usage: dnslookup hostname\n");
        } else if (chos_net_dns_lookup_start(hostname)) {
            uart_puts("DNS query submitted; run 'netstatus' to inspect the result.\n");
        } else {
            uart_puts("DNS query could not start; verify DHCP state and DNS configuration with 'netstatus'.\n");
        }
    } else if (starts_with(line, "ping ")) {
        uint32_t destination;
        if (!parse_ipv4_text(skip_spaces(line + 5u), &destination)) {
            uart_puts("Usage: ping IPv4-address (for example: ping 192.168.1.1)\n");
        } else if (chos_net_ping_start(destination)) {
            uart_puts("ICMP echo started; run 'netstatus' to inspect the result.\n");
        } else {
            uart_puts("Ping could not start; verify DHCP with 'netstatus'.\n");
        }
    } else if (starts_with(line, "tcpconnect ")) {
        command_tcp_connect(line + 11u);
    } else if (strings_equal(line, "tcpstatus")) {
        command_tcp_status();
    } else if (starts_with(line, "tcpsend ")) {
        command_tcp_send(line + 8u);
    } else if (starts_with(line, "tcprecv ")) {
        command_tcp_recv(line + 8u);
    } else if (starts_with(line, "tcpclose ")) {
        command_tcp_close(line + 9u);
    } else if (strings_equal(line, "mount")) {
        if (storage_mount()) uart_puts("[ OK ] CHFS mounted\n"); else uart_puts("[FAIL] mount failed: disk absent, unformatted, or invalid metadata\n");
    } else if (strings_equal(line, "mkfs")) {
        uart_puts("Formatting attached disk as CHFS v3... all existing disk contents will be lost.\n");
        if (storage_format()) uart_puts("[ OK ] CHFS formatted and mounted\n"); else uart_puts("[FAIL] could not format disk\n");
    } else if (strings_equal(line, "fsls")) {
        command_fsls();
    } else if (starts_with(line, "fsread ")) {
        command_fsread(skip_spaces(line + 7));
    } else if (strings_equal(line, "rng-test")) {
        uint8_t bytes[32]; size_t amount=0;
        uint64_t start=timer_counter(), limit=timer_frequency_hz()/10u;
        for (unsigned polls=0;polls<100000u && amount<sizeof(bytes);++polls) {
            amount+=chos_entropy_read(bytes+amount,sizeof(bytes)-amount);
            if (limit && timer_counter()-start>limit) break;
        }
        /* Do not print entropy or manufacture a fallback. This is a transport
         * readiness test, not a statistical/certification test. */
        uart_puts(amount==sizeof(bytes)?"[ OK ] RNG received 32 hardware bytes\n":"[FAIL] RNG hardware bytes unavailable\n");
        for(unsigned i=0;i<sizeof(bytes);++i) ((volatile uint8_t *)bytes)[i]=0;
    } else if (starts_with(line, "fswrite ")) {
        command_fswrite(line + 8);
    } else if (starts_with(line, "fsrm ")) {
        command_fsrm(skip_spaces(line + 5));
    } else if (strings_equal(line, "apps")) {
        print_romfs_files();
    } else if (starts_with(line, "exec ")) {
        run_process_path_command(skip_spaces(line + 5));
    } else if (starts_with(line, "spawn ")) {
        run_spawn_path_command(skip_spaces(line + 6));
    } else if (strings_equal(line, "runqueue")) {
        run_queued_tasks_command();
    } else if (strings_equal(line, "runtime")) {
        if (shell_runtime_active) uart_puts("Already in the interactive scheduled runtime.\n");
        else enter_runtime_session(tree);
    } else if (strings_equal(line, "runtime-exit")) {
        if (shell_runtime_active) {
            uart_puts("Leaving runtime; waiting for queued processes to finish...\n");
            shell_runtime_exit_requested = 1;
        } else {
            uart_puts("Not in runtime mode. Enter 'runtime' first.\n");
        }
    } else if (strings_equal(line, "async-test")) {
        (void)process_async_self_test();
    } else if (strings_equal(line, "romfs-test")) {
        run_romfs_self_test_command();
    } else if (strings_equal(line, "exec-demo")) {
        if (shell_runtime_active) run_spawn_path_command("/BIN/HELLO.CHX");
        else run_process_demo_command();
    } else if (strings_equal(line, "process-test")) {
        run_process_self_test_command();
    } else if (strings_equal(line, "process-preempt-test")) {
        (void)process_preemptive_isolation_test();
    } else if (strings_equal(line, "process-reuse-test")) {
        (void)process_slot_reuse_test();
    } else if (strings_equal(line, "el0-test") || strings_equal(line, "usermode-test")) {
        (void)user_mode_self_test();
    } else if (starts_with(line, "echo ")) {
        arg = line + 5;
        uart_puts(arg);
        uart_puts("\n");
    } else if (strings_equal(line, "echo")) {
        uart_puts("\n");
    } else if (strings_equal(line, "clear")) {
        uart_puts("\033[2J\033[H");
    } else if (strings_equal(line, "gui")) {
        if (chos_gui_ready()) {
            chos_gui_show();
            uart_puts("[ OK ] CHOS desktop redrawn on VirtIO-GPU. Use the QEMU display keyboard/mouse; serial shell remains active.\n");
        } else {
            uart_puts("CHOS desktop is unavailable; use the serial console.\n");
        }
    } else if (strings_equal(line, "exception-test")) {
        uart_puts("Triggering BRK #0 to test the synchronous exception handler...\n");
        __asm__ volatile("brk #0");
    } else if (strings_equal(line, "halt")) {
        uart_puts("CHOS halted. Reset or close the QEMU VM to continue.\n");
        for (;;) __asm__ volatile("wfe");
    } else {
        uart_puts("Unknown command: ");
        uart_puts(line);
        uart_puts("\nType 'help' to list available commands.\n");
    }
}

static void shell_loop(const fdt_info_t *device_tree, int runtime_mode) {
    char line[LINE_CAPACITY];
    uart_puts(runtime_mode
        ? "CHOS scheduled runtime console. 'spawn PATH' starts a background process; 'runtime-exit' returns.\n"
        : "CHOS command console. Type 'help' for commands.\n");
    for (;;) {
        size_t length = 0;
        int overflow = 0;
        size_t idle_polls = 0u;

        if (runtime_mode) {
            process_reap_pending();
            if (shell_runtime_exit_requested) break;
        }

        uart_puts("chos> ");
        for (;;) {
            char c;
            /* The serial shell stays responsive while the graphical input queue is polled. */
            do {
                chos_input_poll();
                chos_net_stack_poll();chos_net_manager_poll();
                if (runtime_mode) {
                    (void)process_service_pending_spawn_request();
                    if ((++idle_polls & 255u) == 0u) process_reap_pending();
                }
                if (uart_try_getc(&c)) break;
                if (timer_is_ready()) __asm__ volatile("wfi" ::: "memory");
                else __asm__ volatile("yield");
            } while (1);

            if (c == '\r' || c == '\n') {
                uart_puts("\n");
                break;
            }

            if (c == '\b' || c == 0x7f) {
                if (length > 0) {
                    --length;
                    uart_puts("\b \b");
                }
                continue;
            }

            if ((unsigned char)c < 0x20u || (unsigned char)c > 0x7eu) continue;

            if (length + 1u < LINE_CAPACITY) {
                line[length++] = c;
                uart_putc(c);
            } else if (!overflow) {
                uart_puts("\nInput too long (maximum 127 characters).\n");
                overflow = 1;
            }
        }

        if (overflow) {
            uart_puts("Command discarded.\n");
            continue;
        }
        line[length] = '\0';
        execute_command(line, device_tree);
    }
}

static void runtime_shell_task_entry(void *argument) {
    shell_runtime_active = 1;
    shell_runtime_exit_requested = 0;
    shell_loop((const fdt_info_t *)argument, 1);
    shell_runtime_active = 0;
}

static void enter_runtime_session(const fdt_info_t *device_tree) {
    uint32_t task_id = 0u;
    if (scheduler_is_running()) {
        uart_puts("Runtime is already active.\n");
        return;
    }
    if (!timer_is_ready()) {
        uart_puts("[FAIL] background runtime requires the ARM Generic Timer.\n");
        return;
    }
    /* Enable the development policy before publishing the first runtime task.
     * If task creation fails, roll the policy back rather than leaving a half-
     * initialized runtime or halting the boot shell. */
    if (!scheduler_set_preemption_enabled(1)) {
        uart_puts("[FAIL] could not enable timer preemption for runtime\n");
        return;
    }
    if (!scheduler_create_task("runtime-shell", runtime_shell_task_entry,
                               (void *)device_tree, &task_id)) {
        (void)scheduler_set_preemption_enabled(0);
        uart_puts("[FAIL] could not create runtime shell task\n");
        return;
    }
    uart_puts("Starting scheduled runtime (shell task id ");
    print_decimal(task_id);
    uart_puts("). Type 'runtime-exit' inside the runtime to return here.\n");
    if (!scheduler_run()) {
        uart_puts("[FAIL] scheduled runtime stopped after a scheduler dispatch error.\n");
    } else {
        uart_puts("[ OK ] runtime shell and scheduled processes finished; returned to boot shell.\n");
    }
    process_reap_pending();
}

void shell_run(const fdt_info_t *device_tree) {
    /* The GUI needs the persistent scheduled runtime so graphical app launches
     * can enqueue EL0 tasks without blocking the input/redraw loop. Keep the
     * original boot shell as the fallback whenever graphics or the timer is absent. */
    if (chos_gui_ready() && timer_is_ready()) enter_runtime_session(device_tree);
    shell_loop(device_tree, 0);
}
