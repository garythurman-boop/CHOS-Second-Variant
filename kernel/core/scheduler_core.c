#include <stddef.h>
#include <stdint.h>
#include "chos/scheduler.h"

int scheduler_pick_next(const scheduler_task_state_t *states, size_t count,
                        size_t after_index, size_t *next_index) {
    size_t start;
    size_t offset;

    if (states == NULL || next_index == NULL || count == 0u) return 0;
    if (after_index == CHOS_SCHED_NO_INDEX || after_index >= count) {
        start = 0u;
    } else {
        start = after_index + 1u;
        if (start == count) start = 0u;
    }

    for (offset = 0u; offset < count; ++offset) {
        size_t index = start + offset;
        if (index >= count) index -= count;
        if (states[index] == SCHED_TASK_READY) {
            *next_index = index;
            return 1;
        }
    }
    return 0;
}

size_t scheduler_wake_due(scheduler_task_state_t *states, uint64_t *wake_ticks,
                          size_t count, uint64_t now_tick) {
    size_t i, woken = 0u;
    if (states == NULL || wake_ticks == NULL) return 0u;
    for (i = 0u; i < count; ++i) {
        /* Deadlines saturate on overflow; CNT ticks will not reach UINT64_MAX
           in a practical boot, and using >= avoids wraparound ambiguity. */
        if (states[i] == SCHED_TASK_SLEEPING && wake_ticks[i] <= now_tick) {
            states[i] = SCHED_TASK_READY;
            wake_ticks[i] = 0u;
            ++woken;
        }
    }
    return woken;
}

int scheduler_wake_sleeping(scheduler_task_state_t *states, uint64_t *wake_ticks,
                            size_t count, size_t index) {
    if (states == NULL || wake_ticks == NULL || index >= count ||
        states[index] != SCHED_TASK_SLEEPING) return 0;
    states[index] = SCHED_TASK_READY;
    wake_ticks[index] = 0u;
    return 1;
}

int scheduler_has_live_tasks(const scheduler_task_state_t *states, size_t count) {
    size_t i;
    if (states == NULL) return 0;
    for (i = 0u; i < count; ++i) {
        if (states[i] == SCHED_TASK_READY || states[i] == SCHED_TASK_RUNNING ||
            states[i] == SCHED_TASK_SLEEPING) return 1;
    }
    return 0;
}


/* Select only dead tasks whose exit has requested deferred cleanup. */
size_t scheduler_collect_reapable(const scheduler_task_state_t *states,
                                  const uint8_t *reap_pending, size_t count,
                                  size_t *out_slots, size_t out_capacity) {
    size_t i, out_count = 0u;
    if (states == NULL || reap_pending == NULL || out_slots == NULL || out_capacity == 0u) {
        return 0u;
    }
    for (i = 0u; i < count && out_count < out_capacity; ++i) {
        if (states[i] == SCHED_TASK_DEAD && reap_pending[i] != 0u) {
            out_slots[out_count++] = i;
        }
    }
    return out_count;
}


int scheduler_slot_reusable(scheduler_task_state_t state, uint8_t reap_pending) {
    if (reap_pending != 0u) return 0;
    return state == SCHED_TASK_UNUSED || state == SCHED_TASK_DEAD;
}

/* Decide what an EL1 timer IRQ should resume. This function touches only scheduler
 * policy arrays, so the transition logic can be tested on the host without hardware. */
scheduler_irq_action_t scheduler_preemption_plan(
    scheduler_task_state_t *states, uint64_t *wake_ticks, size_t count,
    size_t interrupted_slot, uint64_t now_tick, size_t *resume_slot) {
    if (states == NULL || wake_ticks == NULL || resume_slot == NULL || count == 0u) {
        return SCHED_IRQ_FINISHED;
    }

    if (interrupted_slot < count && states[interrupted_slot] == SCHED_TASK_RUNNING) {
        states[interrupted_slot] = SCHED_TASK_READY;
    }
    (void)scheduler_wake_due(states, wake_ticks, count, now_tick);

    if (scheduler_pick_next(states, count, interrupted_slot, resume_slot)) {
        states[*resume_slot] = SCHED_TASK_RUNNING;
        return SCHED_IRQ_RESUME_TASK;
    }
    return scheduler_has_live_tasks(states, count)
        ? SCHED_IRQ_RESUME_IDLE : SCHED_IRQ_FINISHED;
}
