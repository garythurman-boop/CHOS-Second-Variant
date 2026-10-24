#include <stddef.h>
#include <stdint.h>
#include "chos/scheduler.h"
#include "chos/timer.h"
#include "chos/uart.h"

#define SCHED_CONTEXT_GPR_COUNT 12u
#define SCHED_CONTROL_STACK_SIZE (8u * 1024u)
#define SPSR_EL1H_ALL_MASKED UINT64_C(0x3c5)

/* x19..x30 at offsets 0..88; SP at offset 96; matches context_switch.S. */
typedef struct {
    uint64_t x19_x30[SCHED_CONTEXT_GPR_COUNT];
    uint64_t sp;
} scheduler_cpu_context_t;

_Static_assert(offsetof(scheduler_cpu_context_t, sp) == 96u,
               "AArch64 context layout must match context_switch.S");
_Static_assert(sizeof(scheduler_cpu_context_t) == 104u,
               "Unexpected AArch64 context size");
_Static_assert((CHOS_TASK_STACK_SLOT_SIZE % CHOS_TASK_STACK_GUARD_SIZE) == 0u,
               "Task stack slots must remain page aligned");

typedef struct {
    scheduler_cpu_context_t context;
    scheduler_task_entry_t entry;
    void *argument;
    chos_irq_context_t *irq_frame;
    address_space_handle_t address_space;
    int has_address_space;
    uint32_t id;
    char name[CHOS_TASK_NAME_CAPACITY];
} scheduler_task_t;

static scheduler_task_t tasks[CHOS_SCHED_MAX_TASKS];
static scheduler_task_state_t task_states[CHOS_SCHED_MAX_TASKS];
static uint64_t task_wake_ticks[CHOS_SCHED_MAX_TASKS];
static uint8_t task_reap_pending[CHOS_SCHED_MAX_TASKS];
/* Each slot is one unmapped guard page followed by four mapped stack pages. */
static uint8_t task_stack_slots[CHOS_SCHED_MAX_TASKS][CHOS_TASK_STACK_SLOT_SIZE]
    __attribute__((aligned(CHOS_TASK_STACK_GUARD_SIZE), section(".bss.task_stacks")));
static scheduler_cpu_context_t scheduler_context;
static scheduler_cpu_context_t preemptive_boot_context;
static scheduler_cpu_context_t preemptive_idle_context;
static uint8_t preemptive_boot_stack[SCHED_CONTROL_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t preemptive_idle_stack[SCHED_CONTROL_STACK_SIZE] __attribute__((aligned(16)));
static chos_irq_context_t *preemptive_idle_initial_frame;
static chos_irq_context_t *preemptive_idle_resume_frame;
static size_t current_slot = CHOS_SCHED_NO_INDEX;
static uint32_t next_task_id = 1u;
static uint64_t created_tasks;
static uint64_t completed_tasks;
static uint64_t yield_calls;
static uint64_t context_switches;
static uint64_t sleep_calls;
static uint64_t wakeups;
static uint64_t critical_sections;
static uint64_t reaped_tasks;
static volatile uint64_t preemption_irqs;
static volatile uint64_t preemption_switches;
static volatile uint64_t preempted_frames;
static volatile uint64_t address_space_switches;
static volatile uint64_t address_space_switch_failures;
static volatile int scheduler_dispatch_failure;
static int scheduler_initialized;
static volatile int scheduler_running;
static volatile int preemption_requested;

extern void chos_context_switch(scheduler_cpu_context_t *outgoing,
                                const scheduler_cpu_context_t *incoming);
extern void chos_irq_resume(chos_irq_context_t *context) __attribute__((noreturn));

static void scheduler_task_trampoline(void) __attribute__((noreturn));
static void scheduler_preemptive_bootstrap_trampoline(void) __attribute__((noreturn));
static void scheduler_preemptive_idle_trampoline(void) __attribute__((noreturn));


/* Run with local IRQs masked (or from the IRQ dispatcher). Each process root
 * contains the same privileged kernel mappings, so scheduler code/stacks remain
 * accessible before and after TTBR0 changes. */
static int scheduler_activate_task_root(size_t slot) {
    uint64_t before, after;
    address_space_error_t result;
    if (slot >= CHOS_SCHED_MAX_TASKS) return 0;
    before = vmm_current_root_table_address();
    if (tasks[slot].has_address_space) {
        result = address_space_scheduler_activate(tasks[slot].address_space);
    } else {
        result = address_space_scheduler_activate_kernel();
    }
    if (result != AS_OK) {
        ++address_space_switch_failures;
        return 0;
    }
    after = vmm_current_root_table_address();
    if (before != after) ++address_space_switches;
    return 1;
}

static int scheduler_activate_kernel_root(void) {
    uint64_t before = vmm_current_root_table_address();
    address_space_error_t result = scheduler_is_running()
        ? address_space_scheduler_activate_kernel() : address_space_deactivate();
    if (result != AS_OK) {
        ++address_space_switch_failures;
        return 0;
    }
    if (before != vmm_current_root_table_address()) ++address_space_switches;
    return 1;
}

static uint64_t scheduler_critical_enter(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    ++critical_sections;
    return daif;
}

static void scheduler_critical_leave(uint64_t daif) {
    __asm__ volatile("msr daif, %0\n\tisb" :: "r"(daif) : "memory");
}

static void copy_name(char *destination, const char *source) {
    size_t i = 0u;
    if (source == NULL || source[0] == '\0') source = "task";
    while (i + 1u < CHOS_TASK_NAME_CAPACITY && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

static int choose_next(size_t after, size_t *next) {
    return scheduler_pick_next(task_states, CHOS_SCHED_MAX_TASKS, after, next);
}

static void clear_usable_task_stack(size_t slot) {
    volatile uint64_t *words = (volatile uint64_t *)&task_stack_slots[slot][CHOS_TASK_STACK_GUARD_SIZE];
    size_t i;
    for (i = 0u; i < (CHOS_TASK_STACK_SIZE / sizeof(uint64_t)); ++i) words[i] = 0u;
}

static void initialize_task_irq_frame(size_t slot) {
    uintptr_t stack_top = (uintptr_t)&task_stack_slots[slot][CHOS_TASK_STACK_SLOT_SIZE];
    chos_irq_context_t *frame;
    size_t i;
    stack_top &= ~(uintptr_t)0x0fu;
    frame = (chos_irq_context_t *)(stack_top - sizeof(chos_irq_context_t));
    for (i = 0u; i < 31u; ++i) frame->x[i] = 0u;
    frame->elr = (uint64_t)(uintptr_t)scheduler_task_trampoline;
    /* Start EL1h with all DAIF masks set; the trampoline enables IRQs once SP is valid. */
    frame->spsr = SPSR_EL1H_ALL_MASKED;
    frame->esr = 0u;
    frame->sp_el0 = 0u;
    frame->reserved = 0u;
    tasks[slot].irq_frame = frame;
}

static void prepare_preemptive_contexts(void) {
    uintptr_t boot_top = (uintptr_t)&preemptive_boot_stack[sizeof(preemptive_boot_stack)];
    uintptr_t idle_top = (uintptr_t)&preemptive_idle_stack[sizeof(preemptive_idle_stack)];
    size_t i;
    for (i = 0u; i < SCHED_CONTEXT_GPR_COUNT; ++i) {
        preemptive_boot_context.x19_x30[i] = 0u;
        preemptive_idle_context.x19_x30[i] = 0u;
    }
    boot_top &= ~(uintptr_t)0x0fu;
    idle_top &= ~(uintptr_t)0x0fu;
    preemptive_boot_context.sp = (uint64_t)boot_top;
    preemptive_boot_context.x19_x30[11] =
        (uint64_t)(uintptr_t)scheduler_preemptive_bootstrap_trampoline;

    preemptive_idle_initial_frame = (chos_irq_context_t *)(idle_top - sizeof(chos_irq_context_t));
    preemptive_idle_resume_frame = NULL;
    for (i = 0u; i < 31u; ++i) preemptive_idle_initial_frame->x[i] = 0u;
    preemptive_idle_initial_frame->elr = (uint64_t)(uintptr_t)scheduler_preemptive_idle_trampoline;
    preemptive_idle_initial_frame->spsr = SPSR_EL1H_ALL_MASKED;
    preemptive_idle_initial_frame->esr = 0u;
    preemptive_idle_initial_frame->sp_el0 = 0u;
    preemptive_idle_initial_frame->reserved = 0u;
}

/* Called with IRQs masked. The timer ISR updates only its own tick counter. */
static void wake_due_tasks_locked(void) {
    wakeups += scheduler_wake_due(task_states, task_wake_ticks,
                                  CHOS_SCHED_MAX_TASKS, timer_ticks());
}

/* Called only from the scheduler context, never from an exiting task's stack. */
static void reap_dead_tasks_locked(void) {
    size_t slots[CHOS_SCHED_MAX_TASKS];
    size_t count = scheduler_collect_reapable(task_states, task_reap_pending,
                                               CHOS_SCHED_MAX_TASKS, slots,
                                               CHOS_SCHED_MAX_TASKS);
    size_t n;
    for (n = 0u; n < count; ++n) {
        size_t slot = slots[n];
        size_t i;
        /* An address space stays pinned until the task's saved stack/frame can no
         * longer be resumed. A failed release leaves the slot pending, not reusable. */
        if (tasks[slot].has_address_space &&
            address_space_task_release(tasks[slot].address_space) != AS_OK) {
            continue;
        }
        tasks[slot].has_address_space = 0;
        tasks[slot].address_space = (address_space_handle_t){0u, 0u, 0u};
        /* Clear the usable stack only after execution has switched off it.
           Do not touch the unmapped guard page at the start of the slot. */
        clear_usable_task_stack(slot);
        for (i = 0u; i < SCHED_CONTEXT_GPR_COUNT; ++i) {
            tasks[slot].context.x19_x30[i] = 0u;
        }
        tasks[slot].context.sp = 0u;
        tasks[slot].entry = NULL;
        tasks[slot].argument = NULL;
        tasks[slot].irq_frame = NULL;
        task_wake_ticks[slot] = 0u;
        task_reap_pending[slot] = 0u;
        ++reaped_tasks;
    }
}

static uint64_t sleep_deadline(uint64_t now, uint64_t ticks) {
    return ticks > UINT64_MAX - now ? UINT64_MAX : now + ticks;
}

void scheduler_init(void) {
    size_t i;
    uint64_t daif = scheduler_critical_enter();
    for (i = 0u; i < CHOS_SCHED_MAX_TASKS; ++i) {
        task_states[i] = SCHED_TASK_UNUSED;
        task_wake_ticks[i] = 0u;
        task_reap_pending[i] = 0u;
        tasks[i].entry = NULL;
        tasks[i].argument = NULL;
        tasks[i].irq_frame = NULL;
        tasks[i].address_space = (address_space_handle_t){0u, 0u, 0u};
        tasks[i].has_address_space = 0;
        tasks[i].id = 0u;
        tasks[i].name[0] = '\0';
    }
    for (i = 0u; i < SCHED_CONTEXT_GPR_COUNT; ++i) scheduler_context.x19_x30[i] = 0u;
    scheduler_context.sp = 0u;
    current_slot = CHOS_SCHED_NO_INDEX;
    next_task_id = 1u;
    created_tasks = 0u;
    completed_tasks = 0u;
    yield_calls = 0u;
    context_switches = 0u;
    sleep_calls = 0u;
    wakeups = 0u;
    critical_sections = 0u;
    reaped_tasks = 0u;
    preemption_irqs = 0u;
    preemption_switches = 0u;
    preempted_frames = 0u;
    address_space_switches = 0u;
    address_space_switch_failures = 0u;
    scheduler_dispatch_failure = 0;
    preemption_requested = 0;
    scheduler_running = 0;
    scheduler_initialized = 1;
    scheduler_critical_leave(daif);
}

static int scheduler_create_task_internal(const char *name,
                                          scheduler_task_entry_t entry,
                                          void *argument,
                                          const address_space_handle_t *address_space,
                                          uint32_t *task_id_out) {
    size_t slot, i;
    uintptr_t stack_top;
    uint64_t daif = scheduler_critical_enter();

    if (!scheduler_initialized || entry == NULL ||
        (scheduler_running &&
         (current_slot >= CHOS_SCHED_MAX_TASKS ||
          task_states[current_slot] != SCHED_TASK_RUNNING ||
          tasks[current_slot].has_address_space ||
          vmm_current_root_table_address() != vmm_kernel_root_table_address()))) {
        /* During the persistent runtime, only a scheduled kernel-root task may
         * enqueue work. User tasks cannot call this privileged API directly. */
        scheduler_critical_leave(daif);
        return 0;
    }
    for (slot = 0u; slot < CHOS_SCHED_MAX_TASKS; ++slot) {
        if (scheduler_slot_reusable(task_states[slot], task_reap_pending[slot])) break;
    }
    if (slot == CHOS_SCHED_MAX_TASKS) {
        scheduler_critical_leave(daif);
        return 0;
    }
    if (address_space != NULL && address_space_task_retain(*address_space) != AS_OK) {
        scheduler_critical_leave(daif);
        return 0;
    }

    task_reap_pending[slot] = 0u;
    /* Slot reuse is permitted only from the scheduler/console context. */
    clear_usable_task_stack(slot);
    for (i = 0u; i < SCHED_CONTEXT_GPR_COUNT; ++i) tasks[slot].context.x19_x30[i] = 0u;
    stack_top = (uintptr_t)&task_stack_slots[slot][CHOS_TASK_STACK_SLOT_SIZE];
    stack_top &= ~(uintptr_t)0x0fu; /* AAPCS64 requires 16-byte SP alignment. */
    tasks[slot].context.sp = (uint64_t)stack_top;
    tasks[slot].context.x19_x30[11] = (uint64_t)(uintptr_t)scheduler_task_trampoline;
    tasks[slot].entry = entry;
    tasks[slot].argument = argument;
    tasks[slot].has_address_space = address_space != NULL;
    tasks[slot].address_space = address_space != NULL
        ? *address_space : (address_space_handle_t){0u, 0u, 0u};
    initialize_task_irq_frame(slot);
    tasks[slot].id = next_task_id++;
    if (next_task_id == 0u) next_task_id = 1u;
    task_wake_ticks[slot] = 0u;
    copy_name(tasks[slot].name, name);
    task_states[slot] = SCHED_TASK_READY;
    ++created_tasks;
    if (task_id_out != NULL) *task_id_out = tasks[slot].id;
    scheduler_critical_leave(daif);
    return 1;
}

int scheduler_create_task(const char *name, scheduler_task_entry_t entry,
                          void *argument, uint32_t *task_id_out) {
    return scheduler_create_task_internal(name, entry, argument, NULL, task_id_out);
}

int scheduler_create_task_in_address_space(const char *name,
                          scheduler_task_entry_t entry, void *argument,
                          address_space_handle_t address_space,
                          uint32_t *task_id_out) {
    if (address_space.slot >= CHOS_AS_MAX_SPACES || address_space.generation == 0u ||
        address_space.id == 0u) return 0;
    return scheduler_create_task_internal(name, entry, argument, &address_space, task_id_out);
}

int scheduler_run(void) {
    uint64_t daif;
    size_t next;

    daif = scheduler_critical_enter();
    if (!scheduler_initialized || scheduler_running || current_slot != CHOS_SCHED_NO_INDEX) {
        scheduler_critical_leave(daif);
        return 0;
    }
    scheduler_running = 1;
    scheduler_dispatch_failure = 0;

    if (preemption_requested) {
        /* Keep IRQs masked until the first task frame is entered with ERET. */
        prepare_preemptive_contexts();
        chos_context_switch(&scheduler_context, &preemptive_boot_context);
        /* Resumed by the preemptive idle trampoline after every task is dead. */
        reap_dead_tasks_locked();
        current_slot = CHOS_SCHED_NO_INDEX;
        preemption_requested = 0;
        scheduler_running = 0;
        scheduler_critical_leave(daif);
        return scheduler_dispatch_failure ? 0 : 1;
    }

    scheduler_critical_leave(daif);

    /* The scheduler context doubles as the idle context when every live task sleeps. */
    for (;;) {
        daif = scheduler_critical_enter();
        reap_dead_tasks_locked();
        wake_due_tasks_locked();
        if (scheduler_dispatch_failure) {
            scheduler_running = 0;
            current_slot = CHOS_SCHED_NO_INDEX;
            scheduler_critical_leave(daif);
            return 0;
        }
        if (choose_next(CHOS_SCHED_NO_INDEX, &next)) {
            if (!scheduler_activate_task_root(next)) {
                scheduler_dispatch_failure = 1;
                scheduler_running = 0;
                current_slot = CHOS_SCHED_NO_INDEX;
                scheduler_critical_leave(daif);
                return 0;
            }
            task_states[next] = SCHED_TASK_RUNNING;
            current_slot = next;
            ++context_switches;
            chos_context_switch(&scheduler_context, &tasks[next].context);
            scheduler_critical_leave(daif);
            /* A sleeping/exiting task with no ready peers returns here. */
            continue;
        }
        if (scheduler_has_live_tasks(task_states, CHOS_SCHED_MAX_TASKS)) {
            scheduler_critical_leave(daif);
            /* Timer IRQs wake the idle context; sleep deadlines are checked on return. */
            __asm__ volatile("wfi" ::: "memory");
            continue;
        }
        scheduler_running = 0;
        current_slot = CHOS_SCHED_NO_INDEX;
        scheduler_critical_leave(daif);
        return 1;
    }
}

void scheduler_yield(void) {
    size_t current, next;
    uint64_t daif = scheduler_critical_enter();

    if (!scheduler_running || current_slot >= CHOS_SCHED_MAX_TASKS ||
        task_states[current_slot] != SCHED_TASK_RUNNING) {
        scheduler_critical_leave(daif);
        return;
    }
    current = current_slot;
    ++yield_calls;
    wake_due_tasks_locked();
    task_states[current] = SCHED_TASK_READY;
    if (preemption_requested) {
        volatile scheduler_task_state_t *observed_state = &task_states[current];
        /* Wait until an IRQ actually selects this saved frame again before returning. */
        scheduler_critical_leave(daif);
        while (*observed_state != SCHED_TASK_RUNNING) {
            __asm__ volatile("wfi" ::: "memory");
        }
        return;
    }
    if (!choose_next(current, &next) || next == current) {
        task_states[current] = SCHED_TASK_RUNNING;
        scheduler_critical_leave(daif);
        return;
    }
    if (!scheduler_activate_task_root(next)) {
        task_states[current] = SCHED_TASK_RUNNING;
        scheduler_critical_leave(daif);
        return;
    }

    task_states[next] = SCHED_TASK_RUNNING;
    current_slot = next;
    ++context_switches;
    chos_context_switch(&tasks[current].context, &tasks[next].context);
    scheduler_critical_leave(daif);
    /* This task resumes here when it is selected again. */
}

void scheduler_sleep_ticks(uint64_t ticks) {
    size_t current, next;
    uint64_t daif, now;

    if (ticks == 0u) {
        scheduler_yield();
        return;
    }
    /* Without an initialized timer, sleeping could strand the only runnable task. */
    if (!timer_is_ready()) {
        scheduler_yield();
        return;
    }

    daif = scheduler_critical_enter();
    if (!scheduler_running || current_slot >= CHOS_SCHED_MAX_TASKS ||
        task_states[current_slot] != SCHED_TASK_RUNNING) {
        scheduler_critical_leave(daif);
        return;
    }
    current = current_slot;
    now = timer_ticks();
    task_states[current] = SCHED_TASK_SLEEPING;
    task_wake_ticks[current] = sleep_deadline(now, ticks);
    ++sleep_calls;
    wake_due_tasks_locked();

    if (preemption_requested) {
        volatile scheduler_task_state_t *observed_state = &task_states[current];
        scheduler_critical_leave(daif);
        /* The timer IRQ suspends this stack; the call returns only after wakeup. */
        while (*observed_state == SCHED_TASK_SLEEPING) {
            __asm__ volatile("wfi" ::: "memory");
        }
        return;
    }

    if (choose_next(current, &next)) {
        if (!scheduler_activate_task_root(next)) {
            task_states[current] = SCHED_TASK_RUNNING;
            task_wake_ticks[current] = 0u;
            scheduler_critical_leave(daif);
            return;
        }
        task_states[next] = SCHED_TASK_RUNNING;
        current_slot = next;
        ++context_switches;
        chos_context_switch(&tasks[current].context, &tasks[next].context);
        scheduler_critical_leave(daif);
        return;
    }

    /* No ready task remains. The scheduler/console context runs on the kernel root. */
    if (!scheduler_activate_kernel_root()) {
        task_states[current] = SCHED_TASK_RUNNING;
        task_wake_ticks[current] = 0u;
        scheduler_critical_leave(daif);
        return;
    }
    current_slot = CHOS_SCHED_NO_INDEX;
    chos_context_switch(&tasks[current].context, &scheduler_context);
    scheduler_critical_leave(daif);
    /* The task resumes only after a wake path has made it READY and dispatched it. */
}

int scheduler_wake_task(uint32_t task_id) {
    size_t i;
    uint64_t daif = scheduler_critical_enter();
    if (!scheduler_initialized || task_id == 0u) {
        scheduler_critical_leave(daif);
        return 0;
    }
    for (i = 0u; i < CHOS_SCHED_MAX_TASKS; ++i) {
        if (tasks[i].id == task_id &&
            scheduler_wake_sleeping(task_states, task_wake_ticks,
                                    CHOS_SCHED_MAX_TASKS, i)) {
            ++wakeups;
            scheduler_critical_leave(daif);
            return 1;
        }
    }
    scheduler_critical_leave(daif);
    return 0;
}

void scheduler_exit_current(void) {
    size_t current, next;
    uint64_t daif = scheduler_critical_enter();

    if (!scheduler_running || current_slot >= CHOS_SCHED_MAX_TASKS ||
        task_states[current_slot] != SCHED_TASK_RUNNING) {
        scheduler_critical_leave(daif);
        for (;;) __asm__ volatile("wfe");
    }
    current = current_slot;
    task_states[current] = SCHED_TASK_DEAD;
    task_wake_ticks[current] = 0u;
    task_reap_pending[current] = 1u;
    ++completed_tasks;
    wake_due_tasks_locked();

    if (preemption_requested) {
        /* DEAD tasks are never resumed. A timer IRQ transfers to another task or idle. */
        scheduler_critical_leave(daif);
        for (;;) __asm__ volatile("wfi" ::: "memory");
    }

    if (choose_next(current, &next)) {
        if (scheduler_activate_task_root(next)) {
            task_states[next] = SCHED_TASK_RUNNING;
            current_slot = next;
            ++context_switches;
            chos_context_switch(&tasks[current].context, &tasks[next].context);
        } else {
            scheduler_dispatch_failure = 1;
            current_slot = CHOS_SCHED_NO_INDEX;
            (void)scheduler_activate_kernel_root();
            chos_context_switch(&tasks[current].context, &scheduler_context);
        }
    } else {
        if (!scheduler_activate_kernel_root()) {
            scheduler_dispatch_failure = 1;
            scheduler_critical_leave(daif);
            for (;;) __asm__ volatile("wfi" ::: "memory");
        }
        current_slot = CHOS_SCHED_NO_INDEX;
        chos_context_switch(&tasks[current].context, &scheduler_context);
    }

    /* A DEAD task context is never scheduled again. */
    for (;;) __asm__ volatile("wfe");
}

static void scheduler_task_trampoline(void) {
    size_t slot;
    /* New tasks begin while the dispatch handoff still has IRQs masked. */
    __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
    slot = current_slot;
    scheduler_task_entry_t entry;
    void *argument;

    if (slot >= CHOS_SCHED_MAX_TASKS || task_states[slot] != SCHED_TASK_RUNNING) {
        scheduler_exit_current();
    }
    entry = tasks[slot].entry;
    argument = tasks[slot].argument;
    if (entry != NULL) entry(argument);
    scheduler_exit_current();
}

int scheduler_set_preemption_enabled(int enabled) {
    uint64_t daif = scheduler_critical_enter();
    if (!scheduler_initialized || scheduler_running || (enabled && !timer_is_ready())) {
        scheduler_critical_leave(daif);
        return 0;
    }
    preemption_requested = enabled ? 1 : 0;
    scheduler_critical_leave(daif);
    return 1;
}

int scheduler_preemption_enabled(void) {
    return preemption_requested;
}

static void scheduler_preemptive_bootstrap_trampoline(void) {
    size_t next;
    /* The scheduler entered this stack with IRQs masked. */
    wake_due_tasks_locked();
    if (choose_next(CHOS_SCHED_NO_INDEX, &next)) {
        if (scheduler_activate_task_root(next)) {
            task_states[next] = SCHED_TASK_RUNNING;
            current_slot = next;
            ++context_switches;
            chos_irq_resume(tasks[next].irq_frame);
        }
        scheduler_dispatch_failure = 1;
    }
    (void)scheduler_activate_kernel_root();
    scheduler_running = 0;
    current_slot = CHOS_SCHED_NO_INDEX;
    chos_context_switch(&preemptive_boot_context, &scheduler_context);
    for (;;) __asm__ volatile("wfe");
}

static void scheduler_preemptive_idle_trampoline(void) {
    /* Initial idle frame enters with every DAIF mask set. */
    __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
    for (;;) {
        if (!scheduler_running) {
            /* Return to the shell only on the canonical kernel root. */
            (void)scheduler_activate_kernel_root();
            __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
            chos_context_switch(&preemptive_idle_context, &scheduler_context);
            for (;;) __asm__ volatile("wfe" ::: "memory");
        }
        __asm__ volatile("wfi" ::: "memory");
    }
}

chos_irq_context_t *scheduler_on_timer_irq(chos_irq_context_t *interrupted_context) {
    size_t interrupted = current_slot;
    size_t next = CHOS_SCHED_NO_INDEX;
    scheduler_irq_action_t action;
    int interrupted_was_running = 0;

    if (interrupted_context == NULL || !preemption_requested || !scheduler_running) {
        return interrupted_context;
    }
    /* Dynamic aliases exist only in the canonical kernel root. Defer preemption
     * until all aliases are released rather than switching to a root missing them. */
    {
        vmm_stats_t vstats;
        vmm_get_stats(&vstats);
        if (vstats.dynamic_mappings != 0u) return interrupted_context;
    }
    ++preemption_irqs;

    if (interrupted < CHOS_SCHED_MAX_TASKS) {
        scheduler_task_state_t state = task_states[interrupted];
        interrupted_was_running = (state == SCHED_TASK_RUNNING);
        if (state != SCHED_TASK_DEAD && state != SCHED_TASK_UNUSED) {
            tasks[interrupted].irq_frame = interrupted_context;
            ++preempted_frames;
        }
    }

    {
        uint64_t now = timer_ticks();
        size_t i;
        size_t due_count = 0u;
        for (i = 0u; i < CHOS_SCHED_MAX_TASKS; ++i) {
            if (task_states[i] == SCHED_TASK_SLEEPING && task_wake_ticks[i] <= now) {
                ++due_count;
            }
        }
        wakeups += due_count;
        action = scheduler_preemption_plan(task_states, task_wake_ticks,
                                           CHOS_SCHED_MAX_TASKS, interrupted,
                                           now, &next);
    }
    if (action == SCHED_IRQ_RESUME_TASK) {
        if (next >= CHOS_SCHED_MAX_TASKS || tasks[next].irq_frame == NULL) {
            /* This should be impossible: every READY task has an initialized/saved frame. */
            preemption_requested = 0;
            scheduler_running = 0;
            current_slot = CHOS_SCHED_NO_INDEX;
            return interrupted_context;
        }
        if (!scheduler_activate_task_root(next)) {
            /* Never return a sleeping/dead task frame under the kernel root. The
             * interrupted task frame (when it is resumable) is already saved in
             * its task slot; abort preemption and unwind through the idle frame. */
            task_states[next] = SCHED_TASK_READY;
            if (interrupted_was_running && interrupted < CHOS_SCHED_MAX_TASKS) {
                task_states[interrupted] = SCHED_TASK_RUNNING;
                current_slot = interrupted;
                return interrupted_context; /* root did not change */
            }

            scheduler_dispatch_failure = 1;
            preemption_requested = 0;
            scheduler_running = 0;
            current_slot = CHOS_SCHED_NO_INDEX;
            if (!scheduler_activate_kernel_root()) {
                /* The software/hardware root state is inconsistent; fail closed. */
                for (;;) __asm__ volatile("wfi" ::: "memory");
            }
            if (interrupted >= CHOS_SCHED_MAX_TASKS) {
                preemptive_idle_resume_frame = NULL;
                return interrupted_context; /* already interrupted the idle loop */
            }
            if (preemptive_idle_resume_frame != NULL) {
                chos_irq_context_t *resume = preemptive_idle_resume_frame;
                preemptive_idle_resume_frame = NULL;
                return resume;
            }
            if (preemptive_idle_initial_frame != NULL) {
                chos_irq_context_t *resume = preemptive_idle_initial_frame;
                preemptive_idle_initial_frame = NULL;
                return resume;
            }
            for (;;) __asm__ volatile("wfi" ::: "memory");
        }
        if (interrupted >= CHOS_SCHED_MAX_TASKS) {
            /* Preserve the idle-loop continuation while a task runs. */
            preemptive_idle_resume_frame = interrupted_context;
        }
        current_slot = next;
        if (next != interrupted) {
            ++context_switches;
            if (interrupted_was_running) ++preemption_switches;
        }
        return tasks[next].irq_frame;
    }

    current_slot = CHOS_SCHED_NO_INDEX;
    if (action == SCHED_IRQ_FINISHED) scheduler_running = 0;
    if (!scheduler_activate_kernel_root()) {
        scheduler_dispatch_failure = 1;
        scheduler_running = 0;
    }

    if (interrupted >= CHOS_SCHED_MAX_TASKS) {
        /* Returning the live idle frame consumes it; the next IRQ may save a new one. */
        preemptive_idle_resume_frame = NULL;
        return interrupted_context;
    }

    /* A sleeping/dead task cannot resume. Prefer a saved idle continuation; use the
       initial synthetic frame only on first entry, before the idle stack has run. */
    if (preemptive_idle_resume_frame != NULL) {
        chos_irq_context_t *resume = preemptive_idle_resume_frame;
        preemptive_idle_resume_frame = NULL;
        return resume;
    }
    if (preemptive_idle_initial_frame != NULL) {
        chos_irq_context_t *resume = preemptive_idle_initial_frame;
        preemptive_idle_initial_frame = NULL;
        return resume;
    }
    /* Defensive fallback should not occur if idle/task transitions are consistent. */
    scheduler_running = 0;
    preemption_requested = 0;
    return interrupted_context;
}

int scheduler_is_running(void) { return scheduler_running; }
int scheduler_current_is_kernel_task(void) {
    int result;
    uint64_t daif = scheduler_critical_enter();
    result = scheduler_running && current_slot < CHOS_SCHED_MAX_TASKS &&
             task_states[current_slot] == SCHED_TASK_RUNNING &&
             !tasks[current_slot].has_address_space &&
             vmm_current_root_table_address() == vmm_kernel_root_table_address();
    scheduler_critical_leave(daif);
    return result;
}

void scheduler_reap_completed(void) {
    uint64_t daif = scheduler_critical_enter();
    uint64_t current_root = vmm_current_root_table_address();
    uint64_t kernel_root = vmm_kernel_root_table_address();
    int safe_kernel_context = 0;

    /* Reaping is safe either from a live kernel-root task, or after scheduler_run()
     * has returned to its caller on the boot/kernel stack. Never reap from a user
     * address space or while the current task could be using its own stack. */
    if (current_root == kernel_root) {
        if (scheduler_running && current_slot < CHOS_SCHED_MAX_TASKS &&
            task_states[current_slot] == SCHED_TASK_RUNNING &&
            !tasks[current_slot].has_address_space) {
            safe_kernel_context = 1;
        } else if (!scheduler_running && current_slot == CHOS_SCHED_NO_INDEX) {
            safe_kernel_context = 1;
        }
    }
    if (safe_kernel_context) reap_dead_tasks_locked();
    scheduler_critical_leave(daif);
}


void scheduler_get_stats(scheduler_stats_t *out) {
    size_t i;
    uint64_t daif;
    if (out == NULL) return;
    daif = scheduler_critical_enter();
    out->created_tasks = created_tasks;
    out->completed_tasks = completed_tasks;
    out->yield_calls = yield_calls;
    out->context_switches = context_switches;
    out->sleep_calls = sleep_calls;
    out->wakeups = wakeups;
    out->critical_sections = critical_sections;
    out->reaped_tasks = reaped_tasks;
    out->preemption_irqs = preemption_irqs;
    out->preemption_switches = preemption_switches;
    out->preempted_frames = preempted_frames;
    out->address_space_switches = address_space_switches;
    out->address_space_switch_failures = address_space_switch_failures;
    out->pending_reaps = 0u;
    out->address_space_tasks = 0u;
    for (i = 0u; i < CHOS_SCHED_MAX_TASKS; ++i) {
        if (task_reap_pending[i] != 0u) ++out->pending_reaps;
        if (tasks[i].has_address_space && task_states[i] != SCHED_TASK_UNUSED) ++out->address_space_tasks;
    }
    out->task_capacity = CHOS_SCHED_MAX_TASKS;
    out->stack_bytes_per_task = CHOS_TASK_STACK_SIZE;
    out->stack_guard_bytes_per_task = CHOS_TASK_STACK_GUARD_SIZE;
    out->sleeping_tasks = 0u;
    for (i = 0u; i < CHOS_SCHED_MAX_TASKS; ++i) {
        if (task_states[i] == SCHED_TASK_SLEEPING) ++out->sleeping_tasks;
    }
    out->initialized = scheduler_initialized;
    out->running = scheduler_running;
    out->preemption_enabled = preemption_requested;
    scheduler_critical_leave(daif);
}

size_t scheduler_get_task_count(void) {
    size_t i, count = 0u;
    uint64_t daif = scheduler_critical_enter();
    for (i = 0u; i < CHOS_SCHED_MAX_TASKS; ++i) {
        if (task_states[i] != SCHED_TASK_UNUSED) ++count;
    }
    scheduler_critical_leave(daif);
    return count;
}

size_t scheduler_current_task_index(void) {
    size_t result;
    uint64_t daif = scheduler_critical_enter();
    result = (scheduler_running && current_slot < CHOS_SCHED_MAX_TASKS)
        ? current_slot : CHOS_SCHED_NO_INDEX;
    scheduler_critical_leave(daif);
    return result;
}

int scheduler_current_address_space(address_space_handle_t *out) {
    size_t slot;
    uint64_t daif;
    if (out == NULL) return 0;
    daif = scheduler_critical_enter();
    slot = current_slot;
    if (!scheduler_running || slot >= CHOS_SCHED_MAX_TASKS ||
        !tasks[slot].has_address_space ||
        (task_states[slot] != SCHED_TASK_RUNNING && task_states[slot] != SCHED_TASK_READY)) {
        scheduler_critical_leave(daif);
        return 0;
    }
    *out = tasks[slot].address_space;
    scheduler_critical_leave(daif);
    return 1;
}

int scheduler_get_task_info(size_t index, scheduler_task_info_t *out) {
    size_t i;
    uint64_t daif = scheduler_critical_enter();
    if (out == NULL || index >= CHOS_SCHED_MAX_TASKS ||
        task_states[index] == SCHED_TASK_UNUSED) {
        scheduler_critical_leave(daif);
        return 0;
    }
    out->id = tasks[index].id;
    out->state = task_states[index];
    out->wake_tick = task_wake_ticks[index];
    out->stack_guard_address = (uint64_t)(uintptr_t)&task_stack_slots[index][0];
    out->stack_base_address = out->stack_guard_address + CHOS_TASK_STACK_GUARD_SIZE;
    out->has_address_space = tasks[index].has_address_space;
    out->address_space_id = tasks[index].has_address_space ? tasks[index].address_space.id : 0u;
    for (i = 0u; i < CHOS_TASK_NAME_CAPACITY; ++i) out->name[i] = tasks[index].name[i];
    scheduler_critical_leave(daif);
    return 1;
}

const char *scheduler_state_name(scheduler_task_state_t state) {
    switch (state) {
        case SCHED_TASK_UNUSED: return "unused";
        case SCHED_TASK_READY: return "ready";
        case SCHED_TASK_RUNNING: return "running";
        case SCHED_TASK_SLEEPING: return "sleeping";
        case SCHED_TASK_DEAD: return "dead";
        default: return "invalid";
    }
}
