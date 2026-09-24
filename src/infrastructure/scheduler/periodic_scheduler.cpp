#include "infrastructure/scheduler/periodic_scheduler.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace inerxia::infrastructure {

SchedulerLogSink default_scheduler_log_sink() {
    return [](std::string_view message) {
        std::cerr << "[scheduler] " << message << '\n';
    };
}

PeriodicScheduler::PeriodicScheduler(std::chrono::milliseconds interval, Job job,
                                     SchedulerLogSink log)
    : interval_(interval), job_(std::move(job)), log_(std::move(log)) {}

PeriodicScheduler::~PeriodicScheduler() {
    stop();
}

void PeriodicScheduler::start() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (started_) {
            return;
        }
        started_ = true;
        should_stop_ = false;
    }
    if (log_) {
        log_("started (interval=" + std::to_string(interval_.count()) + "ms)");
    }
    worker_ = std::thread([this] { run_loop(); });
}

void PeriodicScheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        should_stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
        if (log_) {
            log_("stopped gracefully");
        }
    }
}

bool PeriodicScheduler::running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return started_;
}

void PeriodicScheduler::run_once() {
    // Same mutex as the worker uses: manual and periodic runs can never overlap.
    std::lock_guard<std::mutex> run_lock(run_mutex_);
    job_();
}

void PeriodicScheduler::run_loop() {
    for (;;) {
        bool stop_requested = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            stop_requested = should_stop_;
        }
        if (stop_requested) {
            break;
        }
        // First run happens immediately, then once every interval. A trailing
        // wait after each completed run means a slow job never overlaps with
        // itself and never causes back-to-back (busy) re-runs.
        run_job();
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            cv_.wait_for(lock, interval_, [this] { return should_stop_; });
            if (should_stop_) {
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        started_ = false;
    }
}

void PeriodicScheduler::run_job() {
    std::lock_guard<std::mutex> run_lock(run_mutex_);
    try {
        job_();
    } catch (const std::exception& error) {
        if (log_) {
            log_(std::string{"job threw: "} + error.what());
        }
    } catch (...) {
        if (log_) {
            log_("job threw a non-standard exception");
        }
    }
}

}  // namespace inerxia::infrastructure