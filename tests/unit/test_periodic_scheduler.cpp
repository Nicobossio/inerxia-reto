// Unit tests for PeriodicScheduler (thread-based, std-only infrastructure).
//
// Timing-based assertions use generous margins so the suite does not flap on a
// busy machine; the scheduler's ordering guarantees (no overlap, no mutations
// after stop) are what we verify, not exact wall-clock cadence.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "infrastructure/scheduler/periodic_scheduler.hpp"

namespace {
using inerxia::infrastructure::PeriodicScheduler;
using inerxia::infrastructure::SchedulerLogSink;
using namespace std::chrono_literals;
using namespace std::chrono;

// Gives up once the predicate holds or the deadline passes, whichever first.
bool wait_until_or_timeout(const std::atomic<int>& value, int target,
                           milliseconds timeout = 3s) {
    const auto deadline = steady_clock::now() + timeout;
    while (value.load() < target && steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }
    return value.load() >= target;
}

// Log sink that records messages; used instead of the stderr sink in tests.
SchedulerLogSink capturing_sink(std::vector<std::string>& out, std::mutex& mutex) {
    return [&](std::string_view message) {
        std::lock_guard<std::mutex> lock(mutex);
        out.emplace_back(message);
    };
}

TEST(PeriodicSchedulerTest, RunsJobRepeatedlyAtConfiguredInterval) {
    std::atomic<int> runs{0};
    PeriodicScheduler scheduler{20ms, [&] { ++runs; }};
    scheduler.start();

    ASSERT_TRUE(wait_until_or_timeout(runs, /*target=*/3));

    scheduler.stop();
    EXPECT_GE(runs.load(), 3);
    EXPECT_FALSE(scheduler.running());
}

TEST(PeriodicSchedulerTest, StopIsGracefulAndNoRunsHappenAfter) {
    std::atomic<int> runs{0};
    PeriodicScheduler scheduler{10ms, [&] { ++runs; }};
    scheduler.start();
    ASSERT_TRUE(wait_until_or_timeout(runs, /*target=*/2));

    scheduler.stop();
    const int after_stop = runs.load();
    std::this_thread::sleep_for(80ms);

    EXPECT_EQ(runs.load(), after_stop);
}

TEST(PeriodicSchedulerTest, StopJoinsSoAnInFlightRunFinishes) {
    std::atomic<int> started{0};
    std::atomic<int> finished{0};
    PeriodicScheduler scheduler{5ms, [&] {
                                    ++started;
                                    std::this_thread::sleep_for(30ms);
                                    ++finished;
                                }};
    scheduler.start();
    ASSERT_TRUE(wait_until_or_timeout(started, /*target=*/1));

    scheduler.stop();

    // join() must have waited for the in-flight 30ms run to complete.
    EXPECT_EQ(finished.load(), started.load());
    EXPECT_GE(started.load(), 1);
}

TEST(PeriodicSchedulerTest, SlowJobNeverRunsConcurrently) {
    std::atomic<int> in_flight{0};
    std::atomic<int> max_concurrent{0};
    std::atomic<int> runs{0};

    PeriodicScheduler scheduler{2ms, [&] {
                                    int current = in_flight.fetch_add(1) + 1;
                                    int observed = max_concurrent.load();
                                    while (current > observed &&
                                           !max_concurrent.compare_exchange_weak(observed, current)) {
                                    }
                                    ++runs;
                                    std::this_thread::sleep_for(20ms);
                                    in_flight.fetch_sub(1);
                                }};
    scheduler.start();

    ASSERT_TRUE(wait_until_or_timeout(runs, /*target=*/5));

    scheduler.stop();
    EXPECT_EQ(max_concurrent.load(), 1);
}

TEST(PeriodicSchedulerTest, ExceptionsAreCaughtAndLoopKeepsRunning) {
    std::atomic<int> runs{0};
    std::atomic<int> ok_runs{0};
    std::vector<std::string> logs;
    std::mutex log_mutex;

    PeriodicScheduler scheduler{
       10ms,
       [&] {
           ++runs;
           if (runs.load() <= 2) {
               throw std::runtime_error("router unavailable (test)");
           }
           ++ok_runs;
       },
       capturing_sink(logs, log_mutex)};
    scheduler.start();

    ASSERT_TRUE(wait_until_or_timeout(ok_runs, /*target=*/3));

    scheduler.stop();

    {
        std::lock_guard<std::mutex> lock(log_mutex);
        bool threw_logged = false;
        for (const auto& message : logs) {
            if (message.find("threw") != std::string::npos) {
                threw_logged = true;
            }
        }
        EXPECT_TRUE(threw_logged);
    }
}

TEST(PeriodicSchedulerTest, RunOncePropagatesJobException) {
    PeriodicScheduler scheduler{1h, [&] { throw std::runtime_error("boom (test)"); },
                                [](std::string_view) {}};

    EXPECT_THROW(scheduler.run_once(), std::runtime_error);
}

TEST(PeriodicSchedulerTest, StartIsIdempotent) {
    std::atomic<int> runs{0};
    PeriodicScheduler scheduler{30ms, [&] { ++runs; }};

    scheduler.start();
    scheduler.start();
    scheduler.start();
    EXPECT_TRUE(scheduler.running());

    std::this_thread::sleep_for(120ms);
    scheduler.stop();

    // One worker -> a bounded number of runs in the window (a double-spawned
    // thread would double the count; interval is 30ms over ~120ms).
    EXPECT_LE(runs.load(), 5);
    EXPECT_FALSE(scheduler.running());
}

TEST(PeriodicSchedulerTest, StopBeforeStartIsANoop) {
    std::atomic<int> runs{0};
    PeriodicScheduler scheduler{1ms, [&] { ++runs; }};
    scheduler.stop();
    scheduler.stop();
    EXPECT_FALSE(scheduler.running());
    std::this_thread::sleep_for(40ms);
    EXPECT_EQ(runs.load(), 0);
}

}  // namespace