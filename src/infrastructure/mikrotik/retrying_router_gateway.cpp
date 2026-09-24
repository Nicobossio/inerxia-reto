#include "infrastructure/mikrotik/retrying_router_gateway.hpp"

#include <cmath>
#include <string>
#include <thread>
#include <utility>

#include "infrastructure/env.hpp"
#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::infrastructure {

const RouterRetrySleeper RetryingRouterGateway::default_sleeper = [](std::chrono::milliseconds delay) {
    std::this_thread::sleep_for(delay);
};

namespace {

double env_positive_double_or(const char* name, double fallback) {
    const std::string value = env_value_or(name, std::to_string(fallback));
    try {
        const double parsed = std::stod(value);
        if (!(parsed > 0.0)) {
            throw std::invalid_argument("not positive");
        }
        return parsed;
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

RetryPolicy RetryPolicy::from_env() {
    RetryPolicy policy;
    policy.max_attempts = env_positive_int_or("MIKROTIK_RETRY_MAX", policy.max_attempts);
    policy.initial_backoff =
        std::chrono::milliseconds{env_positive_int_or("MIKROTIK_RETRY_BACKOFF_MS",
                                                      static_cast<int>(policy.initial_backoff.count()))};
    policy.max_backoff =
        std::chrono::milliseconds{env_positive_int_or("MIKROTIK_RETRY_MAX_BACKOFF_MS",
                                                      static_cast<int>(policy.max_backoff.count()))};
    policy.multiplier = env_positive_double_or("MIKROTIK_RETRY_MULTIPLIER", policy.multiplier);
    return policy;
}

RetryingRouterGateway::RetryingRouterGateway(std::unique_ptr<application::RouterGateway> inner,
                                             RetryPolicy policy, RouterLogSink log,
                                             RouterRetrySleeper sleeper)
    : inner_(std::move(inner)),
      policy_(policy),
      log_(std::move(log)),
      sleeper_(std::move(sleeper)) {}

void RetryingRouterGateway::enableUser(const domain::ContractId& contract_id,
                                       const domain::IPAddress& ip) {
    run_with_retries("enableUser", [&] { inner_->enableUser(contract_id, ip); });
}

void RetryingRouterGateway::disableUser(const domain::ContractId& contract_id,
                                        const domain::IPAddress& ip) {
    run_with_retries("disableUser", [&] { inner_->disableUser(contract_id, ip); });
}

void RetryingRouterGateway::changeSpeedProfile(const domain::ContractId& contract_id,
                                               const domain::IPAddress& ip,
                                               const domain::SpeedProfile& profile) {
    run_with_retries("changeSpeedProfile",
                     [&] { inner_->changeSpeedProfile(contract_id, ip, profile); });
}

void RetryingRouterGateway::run_with_retries(std::string_view operation,
                                             const std::function<void()>& action) const {
    int attempt = 1;
    while (true) {
        try {
            action();
            if (attempt > 1) {
                log("recovered after " + std::to_string(attempt) + " attempt(s): " +
                    std::string(operation));
            }
            return;
        } catch (const std::exception& error) {
            const bool transient = is_transient(error);
            const bool exhausted = attempt >= policy_.max_attempts;
            if (transient && !exhausted) {
                const auto delay = backoff_for(attempt);
                log("attempt " + std::to_string(attempt) + "/" +
                    std::to_string(policy_.max_attempts) + " failed (" + error.what() +
                    "); retrying in " + std::to_string(delay.count()) + "ms");
                sleeper_(delay);
                ++attempt;
                continue;
            }
            if (transient) {
                log("giving up after " + std::to_string(attempt) + " attempt(s): " +
                    std::string(operation) + ": " + error.what());
            } else {
                log("attempt " + std::to_string(attempt) + "/" +
                    std::to_string(policy_.max_attempts) + " failed (" + error.what() +
                    "); permanent error, not retried");
            }
            throw;
        }
    }
}

bool RetryingRouterGateway::is_transient(const std::exception& error) const {
    if (const auto* router_error = dynamic_cast<const RouterOSApiError*>(&error)) {
        if (router_error->http_status().has_value()) {
            const int status = *router_error->http_status();
            return status == 408 || status == 429 || status >= 500;
        }
    }
    // No HTTP status: transport failure (connection refused, timeout, DNS).
    return true;
}

std::chrono::milliseconds RetryingRouterGateway::backoff_for(int failed_attempts) const {
    double delay_ms = static_cast<double>(policy_.initial_backoff.count()) *
                      std::pow(policy_.multiplier, failed_attempts - 1);
    delay_ms = std::min(delay_ms, static_cast<double>(policy_.max_backoff.count()));
    return std::chrono::milliseconds{static_cast<long>(delay_ms)};
}

void RetryingRouterGateway::log(std::string_view message) const {
    if (log_) {
        log_(message);
    }
}

}  // namespace inerxia::infrastructure