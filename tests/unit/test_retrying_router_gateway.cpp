#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "application/ports/RouterGateway.h"
#include "domain/ids.hpp"
#include "env_guard.hpp"
#include "infrastructure/infrastructure_error.hpp"
#include "infrastructure/mikrotik/retrying_router_gateway.hpp"

using namespace inerxia::application;
using namespace inerxia::domain;
using namespace inerxia::infrastructure;

namespace {

constexpr auto kCt = "ct-1";
constexpr auto kIp = "10.20.30.40";

class FakeInnerGateway final : public inerxia::application::RouterGateway {
public:
    std::function<void()> on_call;
    int calls = 0;
    std::vector<std::string> ops;
    std::vector<ContractId> contracts;
    std::vector<IPAddress> addresses;
    std::vector<SpeedProfile> profiles;

    void enableUser(const ContractId& c, const IPAddress& ip) override {
        invoke("enableUser", c, ip);
    }
    void disableUser(const ContractId& c, const IPAddress& ip) override {
        invoke("disableUser", c, ip);
    }
    void changeSpeedProfile(const ContractId& c, const IPAddress& ip,
                            const SpeedProfile& p) override {
        ops.push_back("changeSpeedProfile");
        contracts.push_back(c);
        addresses.push_back(ip);
        profiles.push_back(p);
        ++calls;
        if (on_call) {
            on_call();
        }
    }

private:
    // Records the invocation (counting it as an attempt) *before* running the
    // payload, so a throwing payload still reflects that the operation was
    // issued -- exactly what the retry loop re-issues.
    void invoke(const std::string& op, const ContractId& c, const IPAddress& ip) {
        ops.push_back(op);
        contracts.push_back(c);
        addresses.push_back(ip);
        ++calls;
        if (on_call) {
            on_call();
        }
    }
};

struct Harness {
    FakeInnerGateway* inner = nullptr;
    std::unique_ptr<RetryingRouterGateway> gateway;
    RetryPolicy policy;
    std::vector<std::chrono::milliseconds> sleeps;
    std::vector<std::string> lines;

    explicit Harness(RetryPolicy p = RetryPolicy{}) : policy(p) {
        auto fake = std::make_unique<FakeInnerGateway>();
        inner = fake.get();
        RetryPolicy applied = policy;
        gateway = std::make_unique<RetryingRouterGateway>(
            std::move(fake), applied,
            [this](std::string_view line) { lines.emplace_back(line); },
            [this](std::chrono::milliseconds delay) { sleeps.push_back(delay); });
    }
};

RouterOSApiError transient_http_error(int status) {
    return RouterOSApiError(std::string("RouterOS API error (HTTP ") +
                                std::to_string(status) + ")",
                            status);
}

TEST(RetryingRouterGatewayTest, SucceedsOnFirstAttempt) {
    Harness harness;
    harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp});

    ASSERT_EQ(harness.inner->calls, 1);
    EXPECT_EQ(harness.inner->ops.front(), "enableUser");
    EXPECT_EQ(harness.inner->contracts.front().value(), kCt);
    EXPECT_EQ(harness.inner->addresses.front().value(), kIp);
    EXPECT_TRUE(harness.sleeps.empty());
    EXPECT_TRUE(harness.lines.empty());
}

TEST(RetryingRouterGatewayTest, FailsThenSucceedsWithConfiguredBackoff) {
    Harness harness;
    harness.inner->on_call = [&, once = true]() mutable {
        if (once) {
            once = false;
            throw transient_http_error(503);
        }
    };

    harness.gateway->disableUser(ContractId{kCt}, IPAddress{kIp});

    ASSERT_EQ(harness.inner->calls, 2);
    EXPECT_EQ(harness.inner->ops.front(), "disableUser");
    ASSERT_EQ(harness.sleeps.size(), 1U);
    EXPECT_EQ(harness.sleeps.front(), std::chrono::milliseconds{250});
    ASSERT_EQ(harness.lines.size(), 2U);
    EXPECT_NE(harness.lines[0].find("attempt 1/3 failed"), std::string::npos);
    EXPECT_NE(harness.lines[0].find("retrying in 250ms"), std::string::npos);
    EXPECT_NE(harness.lines[1].find("recovered after 2 attempt(s): disableUser"),
              std::string::npos);
}

TEST(RetryingRouterGatewayTest, RecoveredAttemptIsLogged) {
    Harness harness;
    harness.inner->on_call = [&, once = true]() mutable {
        if (once) {
            once = false;
            throw transient_http_error(500);
        }
    };

    harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp});

    ASSERT_EQ(harness.lines.size(), 2U);
    EXPECT_NE(harness.lines[0].find("attempt 1/3 failed"), std::string::npos);
    EXPECT_NE(harness.lines[1].find("recovered after 2 attempt(s): enableUser"),
              std::string::npos);
}

TEST(RetryingRouterGatewayTest, PermanentHttpErrorIsNotRetried) {
    Harness harness;
    harness.inner->on_call = [&] { throw transient_http_error(401); };

    EXPECT_THROW(harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp}),
                 RouterOSApiError);

    EXPECT_EQ(harness.inner->calls, 1);
    EXPECT_TRUE(harness.sleeps.empty());
    ASSERT_EQ(harness.lines.size(), 1U);
    EXPECT_NE(harness.lines.front().find("permanent error, not retried"), std::string::npos);
}

TEST(RetryingRouterGatewayTest, TimeoutIsTreatedAsTransientAndRetried) {
    Harness harness;
    harness.inner->on_call = [&, once = true]() mutable {
        if (once) {
            once = false;
            throw InfrastructureError("operation timed out");
        }
    };

    harness.gateway->disableUser(ContractId{kCt}, IPAddress{kIp});

    EXPECT_EQ(harness.inner->calls, 2);
    ASSERT_EQ(harness.sleeps.size(), 1U);
    EXPECT_EQ(harness.sleeps.front(), std::chrono::milliseconds{250});
}

TEST(RetryingRouterGatewayTest, NonHttpExceptionIsTreatedAsTransient) {
    Harness harness;
    harness.inner->on_call = [&, count = 0]() mutable {
        throw std::runtime_error("connection reset by peer");
    };

    EXPECT_THROW(harness.gateway->disableUser(ContractId{kCt}, IPAddress{kIp}),
                 std::runtime_error);

    EXPECT_EQ(harness.inner->calls, harness.policy.max_attempts);
    EXPECT_EQ(harness.sleeps.size(), static_cast<std::size_t>(harness.policy.max_attempts - 1));
    EXPECT_NE(harness.lines.back().find("giving up after 3 attempt(s)"), std::string::npos);
}

TEST(RetryingRouterGatewayTest, ExhaustsMaxAttemptsWithExponentialBackoffAndRethrows) {
    Harness harness{RetryPolicy{3, std::chrono::milliseconds{100},
                                std::chrono::milliseconds{1000}, 2.0}};
    harness.inner->on_call = [&] { throw transient_http_error(503); };

    EXPECT_THROW(harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp}),
                 RouterOSApiError);

    EXPECT_EQ(harness.inner->calls, 3);
    ASSERT_EQ(harness.sleeps.size(), 2U);
    EXPECT_EQ(harness.sleeps[0], std::chrono::milliseconds{100});
    EXPECT_EQ(harness.sleeps[1], std::chrono::milliseconds{200});
    EXPECT_EQ(harness.lines.back().find("giving up after 3 attempt(s)"), 0U);
}

TEST(RetryingRouterGatewayTest, BackoffIsCappedAtMax) {
    Harness harness{RetryPolicy{4, std::chrono::milliseconds{100},
                                std::chrono::milliseconds{150}, 2.0}};
    harness.inner->on_call = [&] { throw transient_http_error(429); };

    EXPECT_THROW(harness.gateway->disableUser(ContractId{kCt}, IPAddress{kIp}),
                 RouterOSApiError);

    EXPECT_EQ(harness.inner->calls, 4);
    ASSERT_EQ(harness.sleeps.size(), 3U);
    EXPECT_EQ(harness.sleeps[0], std::chrono::milliseconds{100});
    EXPECT_EQ(harness.sleeps[1], std::chrono::milliseconds{150});
    EXPECT_EQ(harness.sleeps[2], std::chrono::milliseconds{150});
}

TEST(RetryingRouterGatewayTest, MaxAttemptsOneDisablesRetry) {
    Harness harness{RetryPolicy{1}};
    harness.inner->on_call = [&] { throw transient_http_error(503); };

    EXPECT_THROW(harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp}),
                 RouterOSApiError);

    EXPECT_EQ(harness.inner->calls, 1);
    EXPECT_TRUE(harness.sleeps.empty());
}

TEST(RetryingRouterGatewayTest, RepeatedOperationIsForwardedIdempotently) {
    Harness harness;

    harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp});
    harness.gateway->enableUser(ContractId{kCt}, IPAddress{kIp});
    harness.gateway->changeSpeedProfile(ContractId{kCt}, IPAddress{kIp}, SpeedProfile{40, 20});

    ASSERT_EQ(harness.inner->calls, 3);
    EXPECT_EQ(harness.inner->ops[0], "enableUser");
    EXPECT_EQ(harness.inner->ops[1], "enableUser");
    EXPECT_EQ(harness.inner->ops[2], "changeSpeedProfile");
    for (const auto& contract : harness.inner->contracts) {
        EXPECT_EQ(contract.value(), kCt);
    }
    for (const auto& address : harness.inner->addresses) {
        EXPECT_EQ(address.value(), kIp);
    }
    EXPECT_EQ(harness.inner->profiles.back().download_mbps(), 40);
    EXPECT_EQ(harness.inner->profiles.back().upload_mbps(), 20);
    EXPECT_TRUE(harness.sleeps.empty());
}

TEST(RetryingRouterGatewayTest, RetryReissuesSameOperationWithoutDuplicatingEffects) {
    Harness harness;
    harness.inner->on_call = [&, failed = 0]() mutable {
        if (failed < 2) {
            ++failed;
            throw transient_http_error(503);
        }
    };

    harness.gateway->disableUser(ContractId{kCt}, IPAddress{kIp});

    ASSERT_EQ(harness.inner->calls, 3);
    EXPECT_EQ(harness.inner->ops.size(), 3U);
    // The same logical call is re-issued, never executed twice concurrently:
    // idempotent router state converges and the final successful execution is
    // the only one with observable effect.
    for (const auto& op : harness.inner->ops) {
        EXPECT_EQ(op, "disableUser");
    }
    ASSERT_EQ(harness.sleeps.size(), 2U);
    EXPECT_EQ(harness.sleeps.front(), std::chrono::milliseconds{250});
    EXPECT_EQ(harness.lines.back().find("recovered after 3 attempt(s): disableUser"), 0U);
}

TEST(RetryingRouterGatewayTest, RetryPolicyDefaultsAreUsedWhenEnvUnset) {
    EnvGuard max{"MIKROTIK_RETRY_MAX", nullptr};
    EnvGuard backoff{"MIKROTIK_RETRY_BACKOFF_MS", nullptr};
    EnvGuard max_backoff{"MIKROTIK_RETRY_MAX_BACKOFF_MS", nullptr};
    EnvGuard multiplier{"MIKROTIK_RETRY_MULTIPLIER", nullptr};

    const auto policy = RetryPolicy::from_env();

    EXPECT_EQ(policy.max_attempts, 3);
    EXPECT_EQ(policy.initial_backoff, std::chrono::milliseconds{250});
    EXPECT_EQ(policy.max_backoff, std::chrono::milliseconds{2000});
    EXPECT_DOUBLE_EQ(policy.multiplier, 2.0);
}

TEST(RetryingRouterGatewayTest, RetryPolicyReadsEnvOverrides) {
    EnvGuard max{"MIKROTIK_RETRY_MAX", "5"};
    EnvGuard backoff{"MIKROTIK_RETRY_BACKOFF_MS", "500"};
    EnvGuard max_backoff{"MIKROTIK_RETRY_MAX_BACKOFF_MS", "3000"};
    EnvGuard multiplier{"MIKROTIK_RETRY_MULTIPLIER", "1.5"};

    const auto policy = RetryPolicy::from_env();

    EXPECT_EQ(policy.max_attempts, 5);
    EXPECT_EQ(policy.initial_backoff, std::chrono::milliseconds{500});
    EXPECT_EQ(policy.max_backoff, std::chrono::milliseconds{3000});
    EXPECT_DOUBLE_EQ(policy.multiplier, 1.5);
}

}  // namespace