#ifndef CHOS_SCHEDULER_H
#define CHOS_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>
#include "chos/task_stack_layout.h"
#include "chos/irq_context.h"
#include "chos/address_space.h"

#define CHOS_TASK_NAME_CAPACITY 16u
#define CHOS_SCHED_NO_INDEX ((size_t)-1)

typedef enum {
    SCHED_TASK_UNUSED = 0,
    SCHED_TASK_READY,
    SCHED_TASK_RUNNING,
    SCHED_TASK_SLEEPING,
    SCHED_TASK_DEAD
} scheduler_task_state_t;

typedef void (*scheduler_task_entry_t)(void *argument);

typedef struct {
    uint64_t created_tasks;
    uint64_t completed_tasks;
    uint64_t yield_calls;
    uint64_t context_switches;
    uint64_t sleep_calls;
    uint64_t wakeups;
    uint64_t critical_sections;
    uint64_t reaped_tasks;
    uint64_t preemption_irqs;
    uint64_t preemption_switches;
    uint64_t preempted_frames;
    uint64_t address_space_switches;
    uint64_t address_space_switch_failures;
    uint32_t task_capacity;
    uint32_t stack_bytes_per_task;
    uint32_t stack_guard_bytes_per_task;
    uint32_t sleeping_tasks;
    uint32_t pending_reaps;
    uint32_t address_space_tasks;
    int initialized;
    int running;
    int preemption_enabled;
} scheduler_stats_t;

typedef struct {
    uint32_t id;
    scheduler_task_state_t state;
    uint64_t wake_tick;
    uint64_t stack_guard_address;
    uint64_t stack_base_address;
    uint32_t address_space_id;
    int has_address_space;
    char name[CHOS_TASK_NAME_CAPACITY];
} scheduler_task_info_t;

/* Cooperative scheduling is the default. Timer preemption is opt-in and intended
 * for development/testing only until SMP, syscall and device-driver locking exist. */
void scheduler_init(void);
/* `argument` is a borrowed pointer; the caller owns its lifetime and cleanup. */
int scheduler_create_task(const char *name, scheduler_task_entry_t entry,
                          void *argument, uint32_t *task_id_out);
/* Create a task pinned to an address space. The scheduler retains the space until
 * deferred task cleanup after exit. All tasks still execute in EL1 in this milestone. */
int scheduler_create_task_in_address_space(const char *name,
                          scheduler_task_entry_t entry, void *argument,
                          address_space_handle_t address_space,
                          uint32_t *task_id_out);
int scheduler_run(void);       /* Run READY tasks until all of them exit. */
/* Enable/disable development timer preemption before scheduler_run(). Requires timer. */
int scheduler_set_preemption_enabled(int enabled);
int scheduler_preemption_enabled(void);
/* Called by the timer IRQ dispatcher after the GIC interrupt is acknowledged. */
chos_irq_context_t *scheduler_on_timer_irq(chos_irq_context_t *interrupted_context);
void scheduler_yield(void);    /* Voluntary round-robin yield by current task. */
void scheduler_sleep_ticks(uint64_t ticks); /* Cooperative sleep; timer ticks continue in IRQ. */
int scheduler_wake_task(uint32_t task_id);  /* Wake a sleeping task by ID. */
void scheduler_exit_current(void) __attribute__((noreturn));
int scheduler_is_running(void);
/* Reap dead task stacks/address-space pins when called from a live kernel-root task. */
void scheduler_reap_completed(void);
/* True only for the currently running scheduled task with no private address space. */
int scheduler_current_is_kernel_task(void);
void scheduler_get_stats(scheduler_stats_t *out);
size_t scheduler_get_task_count(void);
int scheduler_get_task_info(size_t index, scheduler_task_info_t *out);
/* Current task identity/address space, valid while scheduler_run() is active. */
size_t scheduler_current_task_index(void);
int scheduler_current_address_space(address_space_handle_t *out);
const char *scheduler_state_name(scheduler_task_state_t state);

/* Pure round-robin selector shared with host-side unit tests. `after_index`
 * may be CHOS_SCHED_NO_INDEX to select from slot zero. */
int scheduler_pick_next(const scheduler_task_state_t *states, size_t count,
                        size_t after_index, size_t *next_index);
size_t scheduler_wake_due(scheduler_task_state_t *states, uint64_t *wake_ticks,
                          size_t count, uint64_t now_tick);
size_t scheduler_collect_reapable(const scheduler_task_state_t *states,
                                  const uint8_t *reap_pending, size_t count,
                                  size_t *out_slots, size_t out_capacity);
int scheduler_wake_sleeping(scheduler_task_state_t *states, uint64_t *wake_ticks,
                            size_t count, size_t index);
int scheduler_has_live_tasks(const scheduler_task_state_t *states, size_t count);
/* A dead slot is reusable only after deferred cleanup has completed. */
int scheduler_slot_reusable(scheduler_task_state_t state, uint8_t reap_pending);

typedef enum {
    SCHED_IRQ_RESUME_TASK = 0,
    SCHED_IRQ_RESUME_IDLE,
    SCHED_IRQ_FINISHED
} scheduler_irq_action_t;

/* Pure policy helper used by the IRQ scheduler and host-side tests. */
scheduler_irq_action_t scheduler_preemption_plan(
    scheduler_task_state_t *states, uint64_t *wake_ticks, size_t count,
    size_t interrupted_slot, uint64_t now_tick, size_t *resume_slot);

#endif
