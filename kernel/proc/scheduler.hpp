#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kernel/lib/atomic.hpp"
#include "kernel/proc/process.hpp"
#include "kernel/proc/sync.hpp"

namespace cinux::proc {

class SchedulingClass {
public:
    virtual ~SchedulingClass() = default;

    // --- Run-queue management (required) ---
    virtual void        enqueue(Task* task) = 0;
    virtual void        dequeue(Task* task) = 0;
    virtual Task*       pick_next()         = 0;
    virtual const char* name() const        = 0;

    // --- Policy hooks (optional; defaults preserve legacy behaviour) ---

    // Called once per timer tick for the running task.  Return true to ask the
    // scheduler to preempt (e.g. the time quantum is exhausted); false lets the
    // task keep running.  Lets each class own its own preemption policy instead
    // of hard-coding it in Scheduler::tick.
    virtual bool task_tick(Task* current);

    // Called when a task is forked/cloned so the class can derive the child's
    // scheduling parameters from the parent (e.g. inherit priority).
    virtual void task_fork(Task* parent, Task* child);

    // Deadline tick for deadline-based (real-time) scheduling, or 0 when the
    // class is not deadline-based.  Reserved for future RT scheduling classes.
    virtual uint64_t task_deadline(Task* task);
};

class RoundRobin : public SchedulingClass {
public:
    static constexpr int MAX_TASKS = 64;

    RoundRobin();

    void        enqueue(Task* task) override;
    void        dequeue(Task* task) override;
    Task*       pick_next() override;
    const char* name() const override;

    bool task_tick(Task* current) override;
    void task_fork(Task* parent, Task* child) override;
    // task_deadline is inherited unchanged (0 = not deadline-based).

private:
    Task*    run_queue_[MAX_TASKS];
    int      head_;
    int      tail_;
    int      count_;
    int      quantum_remaining_;  // Ticks left for the currently running task
    Spinlock lock_;
};

class Scheduler {
public:
    static constexpr int MAX_CLASSES        = 4;
    static constexpr int DEFAULT_TIME_SLICE = 2;

    static void  init();
    static void  register_class(SchedulingClass* sched_class);
    static void  add_task(Task* task);
    static void  remove_task(Task* task);
    static void  yield();
    static void  exit_current();
    static void  run_first(Task* boot_task);
    static Task* current();
    static void  set_current(Task* task);
    static bool  is_initialized();

    static void tick();
    static void schedule();
    static void block(Task* task, const char* reason);
    static void unblock(Task* task);

private:
    static void idle_entry();

    static SchedulingClass* classes_[MAX_CLASSES];
    static int              class_count_;
    static Task*            current_;
    static RoundRobin       default_rr_;
    static Task*            idle_task_;
    static bool             initialized_;
    static lib::Atomic<int> tick_count_;
};

}  // namespace cinux::proc
