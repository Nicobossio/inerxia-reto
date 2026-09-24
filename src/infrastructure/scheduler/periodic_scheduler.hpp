#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

namespace inerxia::infrastructure {

// Sink used to emit scheduler messages. Convention mirrors RouterLogSink in the
// MikroTik layer: an optional std::function over string_view.
using SchedulerLogSink = std::function<void(std::string_view)>;

// Writes one-line "[scheduler] ..." messages to stderr.
SchedulerLogSink default_scheduler_log_sink();

// Minimal periodic job runner built on std::thread + std::condition_variable.
// Deliberately no external queue/scheduler infrastructure.
//
// Contract:
//  * start() spawns ONE worker thread (idempotent); it does not block the caller
//    and the HTTP thread is never blocked.
//  * The job runs immediately on start, then once every `interval` after the
//    previous run completed. Overruns are NOT queued: a slow job simply shifts
//    the next run later, so runs never overlap and a straggler cannot starve
//    the process.
//  * run_once() executes the job synchronously in the calling thread and is
//    serialized against the worker (job body is mutex-guarded), so the same job
//    never runs concurrently even when both paths are used.
//  * stop() (and the destructor) request a graceful shutdown and JOIN the worker
//    thread: an in-flight run finishes before stop() returns. Calling stop()
//    from INSIDE the job is safe (it only flags the worker to exit).
//  * Exceptions thrown by the job are caught and logged via the log sink; the
//    loop keeps running. run_once() instead propagates exceptions to its caller.
class PeriodicScheduler {
public:
    using Job = std::function<void()>;

    PeriodicScheduler(std::chrono::milliseconds interval, Job job,
                      SchedulerLogSink log = default_scheduler_log_sink());
    ~PeriodicScheduler();

    PeriodicScheduler(const PeriodicScheduler&) = delete;
    PeriodicScheduler& operator=(const PeriodicScheduler&) = delete;
    PeriodicScheduler(PeriodicScheduler&&) = delete;
    PeriodicScheduler& operator=(PeriodicScheduler&&) = delete;

    // Starts the worker thread (idempotent no-op if already started).
    void start();
    // Requests a graceful stop and joins the worker thread. Idempotent.
    void stop();
    // True while the worker thread is alive.
    bool running() const;
    // Synchronous run in the calling thread; exceptions propagate to the caller.
    void run_once();

private:
    void run_loop();
    void run_job();

    const std::chrono::milliseconds interval_;
    Job job_;
    SchedulerLogSink log_;

    mutable std::mutex state_mutex_;
    std::condition_variable cv_;
    bool started_ = false;
    bool should_stop_ = false;
    std::thread worker_;

    // Serializes execution of the job (worker loop + run_once), guaranteeing no
    // concurrent executions of the same job.
    std::mutex run_mutex_;
};

}  // namespace inerxia::infrastructure