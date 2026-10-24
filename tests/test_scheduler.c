#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "chos/scheduler.h"

static void test_first_ready_from_start(void) {
    scheduler_task_state_t states[] = {
        SCHED_TASK_UNUSED, SCHED_TASK_READY, SCHED_TASK_READY, SCHED_TASK_DEAD
    };
    size_t next = 99u;
    assert(scheduler_pick_next(states, 4u, CHOS_SCHED_NO_INDEX, &next));
    assert(next == 1u);
}

static void test_round_robin_wrap(void) {
    scheduler_task_state_t states[] = {
        SCHED_TASK_READY, SCHED_TASK_DEAD, SCHED_TASK_READY, SCHED_TASK_UNUSED
    };
    size_t next = 99u;
    assert(scheduler_pick_next(states, 4u, 2u, &next));
    assert(next == 0u);
    assert(scheduler_pick_next(states, 4u, 0u, &next));
    assert(next == 2u);
}

static void test_no_ready_tasks(void) {
    scheduler_task_state_t states[] = {
        SCHED_TASK_UNUSED, SCHED_TASK_RUNNING, SCHED_TASK_DEAD
    };
    size_t next = 99u;
    assert(!scheduler_pick_next(states, 3u, 0u, &next));
    assert(next == 99u);
}

static void test_bad_arguments(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_READY};
    size_t next = 99u;
    assert(!scheduler_pick_next(NULL, 1u, CHOS_SCHED_NO_INDEX, &next));
    assert(!scheduler_pick_next(states, 1u, CHOS_SCHED_NO_INDEX, NULL));
    assert(!scheduler_pick_next(states, 0u, CHOS_SCHED_NO_INDEX, &next));
}

static void test_skip_non_ready_slots(void) {
    scheduler_task_state_t states[] = {
        SCHED_TASK_READY, SCHED_TASK_DEAD, SCHED_TASK_UNUSED,
        SCHED_TASK_RUNNING, SCHED_TASK_READY
    };
    size_t next = 99u;
    assert(scheduler_pick_next(states, 5u, 0u, &next));
    assert(next == 4u);
}


static void test_wake_due_deadlines(void) {
    scheduler_task_state_t states[] = {
        SCHED_TASK_SLEEPING, SCHED_TASK_SLEEPING, SCHED_TASK_READY,
        SCHED_TASK_DEAD, SCHED_TASK_SLEEPING
    };
    uint64_t deadlines[] = {3u, 5u, 0u, 0u, 2u};
    assert(scheduler_wake_due(states, deadlines, 5u, 3u) == 2u);
    assert(states[0] == SCHED_TASK_READY && deadlines[0] == 0u);
    assert(states[4] == SCHED_TASK_READY && deadlines[4] == 0u);
    assert(states[1] == SCHED_TASK_SLEEPING && deadlines[1] == 5u);
    assert(states[2] == SCHED_TASK_READY);
    assert(scheduler_wake_due(states, deadlines, 5u, 5u) == 1u);
    assert(states[1] == SCHED_TASK_READY && deadlines[1] == 0u);
    assert(scheduler_wake_due(states, deadlines, 5u, 99u) == 0u);
}

static void test_manual_wake_and_live_state(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_SLEEPING, SCHED_TASK_DEAD, SCHED_TASK_UNUSED};
    uint64_t deadlines[] = {42u, 0u, 0u};
    assert(scheduler_has_live_tasks(states, 3u));
    assert(scheduler_wake_sleeping(states, deadlines, 3u, 0u));
    assert(states[0] == SCHED_TASK_READY && deadlines[0] == 0u);
    assert(!scheduler_wake_sleeping(states, deadlines, 3u, 0u));
    states[0] = SCHED_TASK_DEAD;
    assert(!scheduler_has_live_tasks(states, 3u));
    assert(!scheduler_has_live_tasks(NULL, 3u));
    assert(!scheduler_wake_sleeping(states, deadlines, 3u, 99u));
    assert(!scheduler_wake_sleeping(NULL, deadlines, 3u, 0u));
    assert(!scheduler_wake_sleeping(states, NULL, 3u, 0u));
}

static void test_deferred_reaping_selection(void) {
    scheduler_task_state_t states[] = {
        SCHED_TASK_DEAD, SCHED_TASK_READY, SCHED_TASK_DEAD,
        SCHED_TASK_DEAD, SCHED_TASK_SLEEPING
    };
    const uint8_t pending[] = {1u, 1u, 0u, 1u, 1u};
    size_t slots[4] = {99u, 99u, 99u, 99u};
    assert(scheduler_collect_reapable(states, pending, 5u, slots, 4u) == 2u);
    assert(slots[0] == 0u && slots[1] == 3u);
    slots[0] = 99u;
    assert(scheduler_collect_reapable(states, pending, 5u, slots, 1u) == 1u);
    assert(slots[0] == 0u);
    assert(scheduler_collect_reapable(NULL, pending, 5u, slots, 1u) == 0u);
    assert(scheduler_collect_reapable(states, NULL, 5u, slots, 1u) == 0u);
    assert(scheduler_collect_reapable(states, pending, 5u, NULL, 1u) == 0u);
    assert(scheduler_collect_reapable(states, pending, 5u, slots, 0u) == 0u);
}

static void test_slot_reuse_requires_completed_reap(void) {
    assert(scheduler_slot_reusable(SCHED_TASK_UNUSED, 0u));
    assert(!scheduler_slot_reusable(SCHED_TASK_UNUSED, 1u));
    assert(scheduler_slot_reusable(SCHED_TASK_DEAD, 0u));
    assert(!scheduler_slot_reusable(SCHED_TASK_DEAD, 1u));
    assert(!scheduler_slot_reusable(SCHED_TASK_READY, 0u));
    assert(!scheduler_slot_reusable(SCHED_TASK_RUNNING, 0u));
    assert(!scheduler_slot_reusable(SCHED_TASK_SLEEPING, 0u));
}

static void test_preemptive_irq_plan_switches_round_robin(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_RUNNING, SCHED_TASK_READY, SCHED_TASK_UNUSED};
    uint64_t deadlines[] = {0u, 0u, 0u};
    size_t resume = CHOS_SCHED_NO_INDEX;
    assert(scheduler_preemption_plan(states, deadlines, 3u, 0u, 10u, &resume) == SCHED_IRQ_RESUME_TASK);
    assert(resume == 1u);
    assert(states[0] == SCHED_TASK_READY && states[1] == SCHED_TASK_RUNNING);
}

static void test_preemptive_irq_plan_single_task_resumes_itself(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_RUNNING, SCHED_TASK_UNUSED};
    uint64_t deadlines[] = {0u, 0u};
    size_t resume = CHOS_SCHED_NO_INDEX;
    assert(scheduler_preemption_plan(states, deadlines, 2u, 0u, 10u, &resume) == SCHED_IRQ_RESUME_TASK);
    assert(resume == 0u && states[0] == SCHED_TASK_RUNNING);
}

static void test_preemptive_irq_plan_idle_and_deadline_wakeup(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_SLEEPING, SCHED_TASK_SLEEPING, SCHED_TASK_DEAD};
    uint64_t deadlines[] = {8u, 4u, 0u};
    size_t resume = CHOS_SCHED_NO_INDEX;
    assert(scheduler_preemption_plan(states, deadlines, 3u, 0u, 3u, &resume) == SCHED_IRQ_RESUME_IDLE);
    assert(states[0] == SCHED_TASK_SLEEPING && states[1] == SCHED_TASK_SLEEPING);
    assert(scheduler_preemption_plan(states, deadlines, 3u, CHOS_SCHED_NO_INDEX, 4u, &resume) == SCHED_IRQ_RESUME_TASK);
    assert(resume == 1u && states[1] == SCHED_TASK_RUNNING && states[0] == SCHED_TASK_SLEEPING);
}

static void test_preemptive_irq_plan_finishes_when_all_dead(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_DEAD, SCHED_TASK_UNUSED};
    uint64_t deadlines[] = {0u, 0u};
    size_t resume = CHOS_SCHED_NO_INDEX;
    assert(scheduler_preemption_plan(states, deadlines, 2u, 0u, 10u, &resume) == SCHED_IRQ_FINISHED);
}

static void test_preemptive_irq_plan_bad_arguments(void) {
    scheduler_task_state_t states[] = {SCHED_TASK_READY};
    uint64_t deadlines[] = {0u};
    size_t resume = CHOS_SCHED_NO_INDEX;
    assert(scheduler_preemption_plan(NULL, deadlines, 1u, 0u, 0u, &resume) == SCHED_IRQ_FINISHED);
    assert(scheduler_preemption_plan(states, NULL, 1u, 0u, 0u, &resume) == SCHED_IRQ_FINISHED);
    assert(scheduler_preemption_plan(states, deadlines, 1u, 0u, 0u, NULL) == SCHED_IRQ_FINISHED);
}

int main(void) {
    test_first_ready_from_start();
    test_round_robin_wrap();
    test_no_ready_tasks();
    test_bad_arguments();
    test_skip_non_ready_slots();
    test_wake_due_deadlines();
    test_manual_wake_and_live_state();
    test_deferred_reaping_selection();
    test_slot_reuse_requires_completed_reap();
    test_preemptive_irq_plan_switches_round_robin();
    test_preemptive_irq_plan_single_task_resumes_itself();
    test_preemptive_irq_plan_idle_and_deadline_wakeup();
    test_preemptive_irq_plan_finishes_when_all_dead();
    test_preemptive_irq_plan_bad_arguments();
    puts("scheduler: selection, timer-preemption policy, deadline wakeup, deferred-reaping and safe slot-reuse tests passed");
    return 0;
}
