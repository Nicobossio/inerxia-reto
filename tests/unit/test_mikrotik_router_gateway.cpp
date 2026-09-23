#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "domain/domain_error.hpp"
#include "infrastructure/infrastructure_error.hpp"
#include "infrastructure/mikrotik/MikrotikRouterGateway.h"
#include "http_fakes.hpp"

using namespace inerxia::domain;
using namespace inerxia::infrastructure;
using inerxia::infrastructure::http::HttpMethod;
using inerxia::infrastructure::http::HttpRequest;
using inerxia::infrastructure::http::HttpResponse;
using inerxia::infrastructure::http::test::FakeHttpClient;
using inerxia::infrastructure::http::test::request_body_has;

namespace {

constexpr auto kIp = "10.20.30.40";
constexpr auto kContract = "ct-1";

RouterOSConfig make_config() {
    RouterOSConfig config;
    config.suspended_list = "suspended";
    config.queue_prefix = "inerxia-";
    return config;
}

constexpr auto kSecret = "S3cr3t!";

TEST(MikrotikRouterGatewayTest, DisableAddsIpToSuspendedAddressList) {
    FakeHttpClient client{[](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(R"([])");
        }
        return FakeHttpClient::ok_empty();
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.disableUser(ContractId{kContract}, IPAddress{kIp});

    ASSERT_EQ(client.count(), 2U);
    const auto& get = client.requests()[0];
    EXPECT_EQ(get.method, HttpMethod::Get);
    EXPECT_EQ(get.path, "ip/firewall/address-list?list=suspended&address=10.20.30.40");

    const auto& put = client.requests()[1];
    EXPECT_EQ(put.method, HttpMethod::Put);
    EXPECT_EQ(put.path, "ip/firewall/address-list");
    EXPECT_TRUE(request_body_has(put, "\"list\":\"suspended\""));
    EXPECT_TRUE(request_body_has(put, "\"address\":\"10.20.30.40\""));
    EXPECT_TRUE(request_body_has(put, "\"comment\":\"inerxia:ct-1\""));
}

TEST(MikrotikRouterGatewayTest, DisableIsIdempotentWhenIpAlreadySuspended) {
    FakeHttpClient client{[](const HttpRequest&) {
        return FakeHttpClient::ok(R"([{".":"*A","list":"suspended","address":"10.20.30.40"}])");
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.disableUser(ContractId{kContract}, IPAddress{kIp});

    ASSERT_EQ(client.count(), 1U);
    EXPECT_EQ(client.requests()[0].method, HttpMethod::Get);
}

TEST(MikrotikRouterGatewayTest, DisableUsesConfiguredAddressListName) {
    FakeHttpClient client{[](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(R"([])");
        }
        return FakeHttpClient::ok_empty();
    }};
    auto config = make_config();
    config.suspended_list = "morosos";
    MikrotikRouterGateway gateway{client, config};

    gateway.disableUser(ContractId{kContract}, IPAddress{kIp});

    EXPECT_EQ(client.requests()[0].path,
              "ip/firewall/address-list?list=morosos&address=10.20.30.40");
    EXPECT_TRUE(request_body_has(client.requests()[1], "\"list\":\"morosos\""));
}

TEST(MikrotikRouterGatewayTest, EnableRemovesIpFromSuspendedAddressList) {
    FakeHttpClient client{[](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(R"([{".":"*A","list":"suspended","address":"10.20.30.40"}])");
        }
        return FakeHttpClient::ok_empty();
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.enableUser(ContractId{kContract}, IPAddress{kIp});

    ASSERT_EQ(client.count(), 2U);
    EXPECT_EQ(client.requests()[0].method, HttpMethod::Get);
    EXPECT_EQ(client.requests()[1].method, HttpMethod::Delete);
    EXPECT_EQ(client.requests()[1].path, "ip/firewall/address-list/*A");
}

TEST(MikrotikRouterGatewayTest, EnableIsNoOpWhenIpNotSuspended) {
    FakeHttpClient client{[](const HttpRequest&) { return FakeHttpClient::ok(R"([])"); }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.enableUser(ContractId{kContract}, IPAddress{kIp});

    EXPECT_EQ(client.count(), 1U);
    EXPECT_EQ(client.requests().front().method, HttpMethod::Get);
}

TEST(MikrotikRouterGatewayTest, ChangeSpeedProfileCreatesQueueWhenMissing) {
    const std::string kTarget = "10.20.30.40%2F32";
    FakeHttpClient client{[](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(R"([])");
        }
        return FakeHttpClient::ok_empty();
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.changeSpeedProfile(ContractId{kContract}, IPAddress{kIp},
                                 SpeedProfile{300, 150});

    ASSERT_EQ(client.count(), 2U);
    const auto& get = client.requests()[0];
    EXPECT_EQ(get.path, "queue/simple?target=" + kTarget);

    const auto& put = client.requests()[1];
    EXPECT_EQ(put.method, HttpMethod::Put);
    EXPECT_EQ(put.path, "queue/simple");
    EXPECT_TRUE(request_body_has(put, "\"target\":\"10.20.30.40/32\""));
    EXPECT_TRUE(request_body_has(put, "\"max-limit\":\"300M/150M\""));
    EXPECT_TRUE(request_body_has(put, "\"limit-at\":\"300M/150M\""));
    EXPECT_TRUE(request_body_has(put, "\"name\":\"inerxia-ct-1\""));
    EXPECT_TRUE(request_body_has(put, "\"comment\":\"inerxia:ct-1\""));
}

TEST(MikrotikRouterGatewayTest, ChangeSpeedProfileUpdatesQueueWhenDifferent) {
    FakeHttpClient client{[](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(
                R"([{".":"*B","target":"10.20.30.40/32","max-limit":"100M/50M","limit-at":"100M/50M"}])");
        }
        return FakeHttpClient::ok_empty();
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.changeSpeedProfile(ContractId{kContract}, IPAddress{kIp},
                                 SpeedProfile{300, 150});

    ASSERT_EQ(client.count(), 2U);
    const auto& patch = client.requests()[1];
    EXPECT_EQ(patch.method, HttpMethod::Patch);
    EXPECT_EQ(patch.path, "queue/simple/*B");
    EXPECT_TRUE(request_body_has(patch, "\"max-limit\":\"300M/150M\""));
    EXPECT_TRUE(request_body_has(patch, "\"limit-at\":\"300M/150M\""));
}

TEST(MikrotikRouterGatewayTest, ChangeSpeedProfileIsNoOpWhenRatesMatch) {
    FakeHttpClient client{[](const HttpRequest&) {
        return FakeHttpClient::ok(
            R"([{".":"*B","target":"10.20.30.40/32","max-limit":"300M/150M","limit-at":"300M/150M"}])");
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.changeSpeedProfile(ContractId{kContract}, IPAddress{kIp},
                                 SpeedProfile{300, 150});

    EXPECT_EQ(client.count(), 1U);
    EXPECT_EQ(client.requests().front().method, HttpMethod::Get);
}

TEST(MikrotikRouterGatewayTest, ApiErrorsAreReportedAsInfrastructureErrors) {
    FakeHttpClient client{[](const HttpRequest&) {
        return HttpResponse{401, std::nullopt};
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    EXPECT_THROW(gateway.disableUser(ContractId{kContract}, IPAddress{kIp}),
                 RouterOSApiError);
    EXPECT_THROW(gateway.enableUser(ContractId{kContract}, IPAddress{kIp}),
                 RouterOSApiError);
    EXPECT_THROW(gateway.changeSpeedProfile(ContractId{kContract}, IPAddress{kIp},
                                              SpeedProfile{10, 5}),
                 RouterOSApiError);
}

TEST(MikrotikRouterGatewayTest, EnsureBlockingRuleProvisionsDropRuleOnce) {
    FakeHttpClient client{[](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(R"([])");
        }
        return FakeHttpClient::ok_empty();
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.ensure_blocking_rule();

    ASSERT_EQ(client.count(), 2U);
    EXPECT_EQ(client.requests()[0].path,
              "ip/firewall/filter?chain=forward&action=drop&src-address-list=suspended");

    const auto& put = client.requests()[1];
    EXPECT_EQ(put.method, HttpMethod::Put);
    EXPECT_EQ(put.path, "ip/firewall/filter");
    EXPECT_TRUE(request_body_has(put, "\"chain\":\"forward\""));
    EXPECT_TRUE(request_body_has(put, "\"action\":\"drop\""));
    EXPECT_TRUE(request_body_has(put, "\"src-address-list\":\"suspended\""));
}

TEST(MikrotikRouterGatewayTest, EnsureBlockingRuleIsIdempotent) {
    FakeHttpClient client{[](const HttpRequest&) {
        return FakeHttpClient::ok(
            R"([{".":"*C","chain":"forward","action":"drop","src-address-list":"suspended"}])");
    }};
    MikrotikRouterGateway gateway{client, make_config()};

    gateway.ensure_blocking_rule();

    EXPECT_EQ(client.count(), 1U);
    EXPECT_EQ(client.requests().front().method, HttpMethod::Get);
}

class EnvGuard {
public:
    EnvGuard(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name_)) {
            saved_ = previous;
        }
        if (value == nullptr) {
            unsetenv(name_);
        } else {
            setenv(name_, value, 1);
        }
    }

    ~EnvGuard() {
        if (saved_.has_value()) {
            setenv(name_, saved_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> saved_;
};

TEST(MikrotikRouterGatewayTest, RouterOSConfigDefaultsAreUsedWhenEnvUnset) {
    EnvGuard clear_list{"MIKROTIK_SUSPENDED_LIST", nullptr};
    EnvGuard clear_queue{"MIKROTIK_QUEUE_PREFIX", nullptr};
    EnvGuard clear_comment{"MIKROTIK_CONTRACT_COMMENT_PREFIX", nullptr};
    EnvGuard clear_rule{"MIKROTIK_BLOCKING_RULE_COMMENT", nullptr};

    const auto config = RouterOSConfig::from_env();

    EXPECT_EQ(config.suspended_list, "suspended");
    EXPECT_EQ(config.queue_prefix, "inerxia-");
    EXPECT_EQ(config.contract_comment_prefix, "inerxia:");
    EXPECT_EQ(config.blocking_rule_comment, "inerxia:baja-automatica");
}

TEST(MikrotikRouterGatewayTest, RouterOSConfigReadsEnvOverrides) {
    EnvGuard list{"MIKROTIK_SUSPENDED_LIST", "morosos"};
    EnvGuard queue{"MIKROTIK_QUEUE_PREFIX", "isp-"};
    EnvGuard comment{"MIKROTIK_CONTRACT_COMMENT_PREFIX", "isp:"};
    EnvGuard rule{"MIKROTIK_BLOCKING_RULE_COMMENT", "regla-baja"};

    const auto config = RouterOSConfig::from_env();

    EXPECT_EQ(config.suspended_list, "morosos");
    EXPECT_EQ(config.queue_prefix, "isp-");
    EXPECT_EQ(config.contract_comment_prefix, "isp:");
    EXPECT_EQ(config.blocking_rule_comment, "regla-baja");
}

TEST(MikrotikRouterGatewayTest, GatewayLogsSanitizedLinesWithoutPayloads) {
    const std::string secret_body =
        std::string(R"([{".":"*T","pwd":")") + kSecret + "\"}]";
    FakeHttpClient client{[&secret_body](const HttpRequest& request) {
        if (request.method == HttpMethod::Get) {
            return FakeHttpClient::ok(R"([])");
        }
        return HttpResponse{200, secret_body};
    }};

    std::vector<std::string> lines;
    MikrotikRouterGateway gateway{client, make_config(),
                                  [&lines](std::string_view line) {
                                      lines.emplace_back(line);
                                  }};

    gateway.disableUser(ContractId{kContract}, IPAddress{kIp});

    ASSERT_EQ(lines.size(), 2U);
    for (const auto& line : lines) {
        EXPECT_TRUE(line.find("RouterOS") != std::string::npos);
        EXPECT_TRUE(line.find("HTTP") != std::string::npos);
        EXPECT_EQ(line.find(kSecret), std::string::npos) << "payload leaked: " << line;
        EXPECT_EQ(line.find("://"), std::string::npos) << "URL leaked: " << line;
        EXPECT_EQ(line.find('"'), std::string::npos) << "JSON leaked: " << line;
    }
    EXPECT_TRUE(lines[0].find("GET ip/firewall/address-list") != std::string::npos);
    EXPECT_TRUE(lines[1].find("PUT ip/firewall/address-list") != std::string::npos);
}

TEST(MikrotikRouterGatewayTest, ConnectionErrorsAreLoggedAndPropagated) {
    FakeHttpClient client{[](const HttpRequest&) -> HttpResponse {
        throw InfrastructureError("connection refused");
    }};

    std::vector<std::string> lines;
    MikrotikRouterGateway gateway{client, make_config(),
                                  [&lines](std::string_view line) {
                                      lines.emplace_back(line);
                                  }};

    EXPECT_THROW(gateway.disableUser(ContractId{kContract}, IPAddress{kIp}),
                 InfrastructureError);

    ASSERT_FALSE(lines.empty());
    EXPECT_TRUE(lines.front().find("connection refused") != std::string::npos);
    EXPECT_EQ(lines.front().find(kSecret), std::string::npos);
}

}  // namespace