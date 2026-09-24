#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

#include "application/ports/RouterGateway.h"
#include "infrastructure/mikrotik/MikrotikRouterGateway.h"

namespace inerxia::infrastructure {

// Retry policy for MikroTik adapter calls. Expressed as total attempts:
// max_attempts = 1 disables retrying entirely (a single try, no backoff).
struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds initial_backoff{250};
    std::chrono::milliseconds max_backoff{2000};
    double multiplier = 2.0;

    // Reads MIKROTIK_RETRY_MAX, MIKROTIK_RETRY_BACKOFF_MS,
    // MIKROTIK_RETRY_MAX_BACKOFF_MS, MIKROTIK_RETRY_MULTIPLIER. Invalid
    // values fall back to the defaults without aborting startup.
    static RetryPolicy from_env();
};

using RouterRetrySleeper = std::function<void(std::chrono::milliseconds)>;

// Decorator adding bounded, configurable retries to any RouterGateway.
//
// Only transient failures are retried:
//   * transport errors (connection refused, timeout, DNS) -- no HTTP status
//   * HTTP 408, 429 and 5xx
// Permanent HTTP errors (4xx other than 408/429) fail immediately.
//
// Every RouterGateway operation is idempotent on the router (entries are
// created once and state converges), so re-issuing the same command during a
// retry never duplicates effects. After the last attempt the original error
// is rethrown so existing 503 mapping stays intact.
class RetryingRouterGateway final : public application::RouterGateway {
public:
    RetryingRouterGateway(std::unique_ptr<application::RouterGateway> inner, RetryPolicy policy,
                          RouterLogSink log = RouterLogSink{},
                          RouterRetrySleeper sleeper = default_sleeper);

    void enableUser(const domain::ContractId&, const domain::IPAddress&) override;
    void disableUser(const domain::ContractId&, const domain::IPAddress&) override;
    void changeSpeedProfile(const domain::ContractId&, const domain::IPAddress&,
                            const domain::SpeedProfile&) override;

private:
    static const RouterRetrySleeper default_sleeper;

    void run_with_retries(std::string_view operation,
                          const std::function<void()>& action) const;
    [[nodiscard]] bool is_transient(const std::exception&) const;
    [[nodiscard]] std::chrono::milliseconds backoff_for(int failed_attempts) const;
    void log(std::string_view message) const;

    std::unique_ptr<application::RouterGateway> inner_;
    RetryPolicy policy_;
    RouterLogSink log_;
    RouterRetrySleeper sleeper_;
};

}  // namespace inerxia::infrastructure