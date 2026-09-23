#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>

#include "env_guard.hpp"
#include "infrastructure/env.hpp"
#include "infrastructure/infrastructure_error.hpp"
#include "infrastructure/mikrotik/MikrotikEnvConfig.h"

using namespace inerxia::infrastructure;

namespace {

constexpr auto kBaseUrl = "https://router.lan:443/rest";
constexpr auto kUser = "router-admin";
constexpr auto kPassword = "local-only-secret";

void set_only_required(bool blank = false) {
    if (blank) {
        setenv("MIKROTIK_BASE_URL", "   ", 1);
        setenv("MIKROTIK_USER", "", 1);
        setenv("MIKROTIK_PASSWORD", " ", 1);
    } else {
        setenv("MIKROTIK_BASE_URL", kBaseUrl, 1);
        setenv("MIKROTIK_USER", kUser, 1);
        setenv("MIKROTIK_PASSWORD", kPassword, 1);
    }
}

void clear_optionals() {
    unsetenv("MIKROTIK_CONNECT_TIMEOUT_SECONDS");
    unsetenv("MIKROTIK_TIMEOUT_SECONDS");
    unsetenv("MIKROTIK_VERIFY_TLS");
    unsetenv("MIKROTIK_SUSPENDED_LIST");
    unsetenv("MIKROTIK_QUEUE_PREFIX");
    unsetenv("MIKROTIK_CONTRACT_COMMENT_PREFIX");
    unsetenv("MIKROTIK_BLOCKING_RULE_COMMENT");
}

TEST(MikrotikEnvConfigTest, LoadsAllVariablesFromEnv) {
    EnvGuard base{"MIKROTIK_BASE_URL", "https://router.lan:443/rest"};
    EnvGuard user{"MIKROTIK_USER", "router-admin"};
    EnvGuard pass{"MIKROTIK_PASSWORD", "local-only-secret"};
    EnvGuard connect{"MIKROTIK_CONNECT_TIMEOUT_SECONDS", "5"};
    EnvGuard total{"MIKROTIK_TIMEOUT_SECONDS", "15"};
    EnvGuard tls{"MIKROTIK_VERIFY_TLS", "false"};
    EnvGuard list{"MIKROTIK_SUSPENDED_LIST", "morosos"};
    EnvGuard queue{"MIKROTIK_QUEUE_PREFIX", "isp-"};
    EnvGuard comment{"MIKROTIK_CONTRACT_COMMENT_PREFIX", "isp:"};
    EnvGuard rule{"MIKROTIK_BLOCKING_RULE_COMMENT", "regla-baja"};

    const auto config = MikrotikEnvConfig::from_env();

    EXPECT_EQ(config.base_url, "https://router.lan:443/rest");
    EXPECT_EQ(config.username, "router-admin");
    EXPECT_EQ(config.password, "local-only-secret");
    EXPECT_EQ(config.connect_timeout_seconds, 5);
    EXPECT_EQ(config.request_timeout_seconds, 15);
    EXPECT_FALSE(config.verify_tls);
    EXPECT_EQ(config.suspended_list, "morosos");
    EXPECT_EQ(config.queue_prefix, "isp-");
    EXPECT_EQ(config.contract_comment_prefix, "isp:");
    EXPECT_EQ(config.blocking_rule_comment, "regla-baja");
}

TEST(MikrotikEnvConfigTest, UsesDefaultsForOptionalVariables) {
    set_only_required();
    EnvGuard u1{"MIKROTIK_CONNECT_TIMEOUT_SECONDS", nullptr};
    EnvGuard u2{"MIKROTIK_TIMEOUT_SECONDS", nullptr};
    EnvGuard u3{"MIKROTIK_VERIFY_TLS", nullptr};
    EnvGuard u4{"MIKROTIK_SUSPENDED_LIST", nullptr};
    EnvGuard u5{"MIKROTIK_QUEUE_PREFIX", nullptr};
    EnvGuard u6{"MIKROTIK_CONTRACT_COMMENT_PREFIX", nullptr};
    EnvGuard u7{"MIKROTIK_BLOCKING_RULE_COMMENT", nullptr};

    const auto config = MikrotikEnvConfig::from_env();

    EXPECT_EQ(config.base_url, kBaseUrl);
    EXPECT_EQ(config.username, kUser);
    EXPECT_EQ(config.password, kPassword);
    EXPECT_EQ(config.connect_timeout_seconds, 10);
    EXPECT_EQ(config.request_timeout_seconds, 30);
    EXPECT_TRUE(config.verify_tls);
    EXPECT_EQ(config.suspended_list, "suspended");
    EXPECT_EQ(config.queue_prefix, "inerxia-");
    EXPECT_EQ(config.contract_comment_prefix, "inerxia:");
    EXPECT_EQ(config.blocking_rule_comment, "inerxia:baja-automatica");
}

TEST(MikrotikEnvConfigTest, ThrowsListingAllMissingRequiredVariables) {
    EnvGuard u1{"MIKROTIK_BASE_URL", nullptr};
    EnvGuard u2{"MIKROTIK_USER", nullptr};
    EnvGuard u3{"MIKROTIK_PASSWORD", nullptr};
    clear_optionals();

    EXPECT_THROW(MikrotikEnvConfig::from_env(), InfrastructureError);

    try {
        MikrotikEnvConfig::from_env();
        FAIL() << "expected from_env() to throw";
    } catch (const InfrastructureError& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("MIKROTIK_BASE_URL"), std::string::npos);
        EXPECT_NE(message.find("MIKROTIK_USER"), std::string::npos);
        EXPECT_NE(message.find("MIKROTIK_PASSWORD"), std::string::npos);
    }
}

TEST(MikrotikEnvConfigTest, ThrowsWhenRequiredVariableIsBlank) {
    set_only_required(/*blank=*/true);
    clear_optionals();

    EXPECT_THROW(MikrotikEnvConfig::from_env(), InfrastructureError);
}

TEST(MikrotikEnvConfigTest, RejectsInvalidTimeout) {
    set_only_required();
    EnvGuard bad{"MIKROTIK_TIMEOUT_SECONDS", "abc"};

    EXPECT_THROW(MikrotikEnvConfig::from_env(), InfrastructureError);
}

TEST(MikrotikEnvConfigTest, RejectsNonPositiveTimeout) {
    set_only_required();
    EnvGuard zero{"MIKROTIK_TIMEOUT_SECONDS", "0"};

    EXPECT_THROW(MikrotikEnvConfig::from_env(), InfrastructureError);
}

TEST(MikrotikEnvConfigTest, VerifiesPasswordIsNeverPartOfErrorMessage) {
    set_only_required();
    EnvGuard bad{"MIKROTIK_CONNECT_TIMEOUT_SECONDS", "x"};

    try {
        MikrotikEnvConfig::from_env();
        FAIL() << "expected from_env() to throw";
    } catch (const InfrastructureError& error) {
        EXPECT_EQ(std::string{error.what()}.find(kPassword), std::string::npos);
    }
}

}  // namespace