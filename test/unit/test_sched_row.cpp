#include "../framework/framework.hpp"
#include "cinux/memory.hpp"
#include "kernel/proc/proc_config.hpp"
#include "kernel/proc/sched_class.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/task.hpp"
#include "test_assert.hpp"
#include "test_case.hpp"  // NOLINT(misc-include-cleaner) consumed by the TEST() macro body

namespace {

// The ready-row overflow story: a full row used to halt the machine at
// five call sites. The row now refuses the transition, counts it, and
// the machine keeps running; a freed slot takes the next arrival.
void (*g_row_body)() = nullptr;

class RowSink {
public:
    void switch_tasks(cinux::proc::Task& outgoing, cinux::proc::Task& incoming);
    void enter_from_main(cinux::proc::Task& incoming);
    void return_to_main(cinux::proc::Task& outgoing);
    void reclaim(cinux::proc::Task& dead);

private:
    void run_body_of(cinux::proc::Task& incoming);
};

class RowWorld {
public:
    RowWorld() {
        kin.name          = "kin";
        runner.name       = "row";
        runner.stack_base = 0x1000ULL * 0x40;
        runner.parent     = &kin;
        cinux::base::CopyBytes(runner.limits, cinux::proc::kDefaultResourceLimits,
                               sizeof(runner.limits));
        cinux::proc::Scheduler::self().init(sink);
        cinux::proc::Scheduler::self().install_class(cinux::proc::DefaultFifoClass());
        cinux::proc::Scheduler::self().seat(runner);
    }

    ~RowWorld() {
        cinux::proc::Scheduler::self().seat(kin);
        cinux::proc::Scheduler::self().run_until_done();
    }

    void run(void (*body)()) {
        g_row_body = body;
        cinux::proc::Scheduler::self().run_until_done();
    }

    RowSink            sink;
    cinux::proc::Task kin;
    cinux::proc::Task runner;
};

void RowSink::run_body_of([[maybe_unused]] cinux::proc::Task& incoming) {
    if (g_row_body != nullptr) {
        void (*const kBody)() = g_row_body;
        g_row_body            = nullptr;
        kBody();
        return;
    }
    cinux::proc::Scheduler::self().exit_current();
}

void RowSink::enter_from_main(cinux::proc::Task& incoming) {
    run_body_of(incoming);
}

void RowSink::switch_tasks(cinux::proc::Task& outgoing, cinux::proc::Task& incoming) {
    static_cast<void>(outgoing);
    run_body_of(incoming);
}

void RowSink::return_to_main(cinux::proc::Task& outgoing) {
    static_cast<void>(outgoing);
}

void RowSink::reclaim(cinux::proc::Task& dead) {
    static_cast<void>(dead);
}

cinux::proc::Task g_fillers[cinux::proc::kReadyQueueCapacity + 1];
cinux::proc::Task g_sleeper;

// NOLINTNEXTLINE(readability-function-cognitive-complexity) a body walks one story in order
void run_row_overflow_body() {
    using namespace cinux::proc;
    auto&      scheduler = Scheduler::self();
    Task* const kSelf    = scheduler.current();
    ASSERT_TRUE(kSelf != nullptr);

    // The runner holds no slot while running, so the row stands empty:
    // fill every slot with seated fillers.
    for (unsigned long index = 0; index < kReadyQueueCapacity; ++index) {
        g_fillers[index].name = "filler";
        ASSERT_TRUE(scheduler.seat(g_fillers[index]));
    }

    // A runnable seat past capacity is refused: the newcomer stays
    // blocked, the refusal counts, and the machine keeps running.
    g_sleeper.name = "sleeper";
    ASSERT_TRUE(scheduler.seat(g_sleeper, false));
    g_fillers[kReadyQueueCapacity].name = "extra";
    const unsigned long long kBefore    = scheduler.ready_refusals();
    ASSERT_FALSE(scheduler.seat(g_fillers[kReadyQueueCapacity]));
    ASSERT_EQ(g_fillers[kReadyQueueCapacity].state, TaskState::kBlocked);
    ASSERT_EQ(scheduler.ready_refusals(), kBefore + 1ULL);

    // A wakeup into a full row is dropped and counted; the sleeper
    // stays blocked and consistent.
    scheduler.unblock(g_sleeper);
    ASSERT_EQ(g_sleeper.state, TaskState::kBlocked);
    ASSERT_EQ(scheduler.ready_refusals(), kBefore + 2ULL);

    // A yielding task the row cannot hold keeps the CPU instead of
    // switching away from itself.
    scheduler.yield();
    ASSERT_EQ(scheduler.current(), kSelf);
    ASSERT_EQ(kSelf->state, TaskState::kRunning);
    ASSERT_EQ(scheduler.ready_refusals(), kBefore + 3ULL);

    // One withdrawn filler frees a slot; the next wakeup lands.
    ASSERT_TRUE(scheduler.withdraw(g_fillers[0]));
    scheduler.unblock(g_sleeper);
    ASSERT_EQ(g_sleeper.state, TaskState::kReady);
    ASSERT_EQ(scheduler.ready_refusals(), kBefore + 3ULL);

    scheduler.exit_current();
}

TEST("sched row: overflow refuses, counts, and recovers without halting") {
    RowWorld world;
    world.run(run_row_overflow_body);
}

}  // namespace

int main() {
    return cinux::test::RunAll();
}
