#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "domain/ids.hpp"
#include "domain/ip_address.hpp"
#include "domain/speed_profile.hpp"
#include "infrastructure/http/curl_http_client.hpp"
#include "infrastructure/http/http_client.hpp"
#include "infrastructure/mikrotik/MikrotikRouterGateway.h"
#include "infrastructure/mikrotik/router_gateway_factory.hpp"

namespace inerxia::infrastructure::http {
namespace {

using domain::ContractId;
using domain::IPAddress;
using domain::SpeedProfile;

auto from_env(const char* name) -> std::optional<std::string> {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

// Live test against a real RouterOS (CHR via QEMU or a physical box).
// Environment-gated: it skips unless MIKROTIK_BASE_URL/USER/PASSWORD are set,
// so CI runs without a router are green by skipping. Uses the production
// libcurl transport and the production gateway, never fakes.
class MikrotikLiveGatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto base_url = from_env("MIKROTIK_BASE_URL");
        const auto user = from_env("MIKROTIK_USER");
        const auto password = from_env("MIKROTIK_PASSWORD");
        if (!base_url || !user || !password) {
            GTEST_SKIP() << "Live MikroTik test requires MIKROTIK_BASE_URL, "
                            "MIKROTIK_USER and MIKROTIK_PASSWORD environment variables";
        }

        http::CurlHttpClientConfig config;
        config.base_url = *base_url;
        config.username = *user;
        config.password = *password;
        transport_ = std::make_unique<http::CurlHttpClient>(std::move(config));

        gateway_ = std::make_unique<MikrotikRouterGateway>(
            *transport_, RouterOSConfig::from_env(), default_router_log_sink());

        disable_log_ = RouterOSConfig::from_env().suspended_list;
        queue_prefix_ = RouterOSConfig::from_env().queue_prefix;
        blocking_comment_ = RouterOSConfig::from_env().blocking_rule_comment;

        gateway_->ensure_blocking_rule();
    }

    void TearDown() override {
        if (!transport_) {
            return;
        }
        cleanup_address_list(ip_);
        cleanup_speed_queue(ip_);
        cleanup_blocking_rule();
    }

    // We never do business logic here, only state assertions. Paths are relative to
    // the REST base URL (e.g. "ip/firewall/filter"): with the transport, a leading
    // slash would produce a "//" path that RouterOS rejects with HTTP 400.
    [[nodiscard]] auto raw_get(std::string_view path) const -> nlohmann::json {
        const auto response =
            transport_->send(HttpRequest{HttpMethod::Get, strip_leading_slash(path), std::nullopt});
        if (!response.ok()) {
            throw std::runtime_error("REST GET " + std::string(path) + " -> HTTP " +
                                     std::to_string(response.status_code));
        }
        nlohmann::json body = nlohmann::json::parse(response.body.value_or("[]"));
        return body;
    }

    void raw_delete(std::string_view path_with_id) {
        const auto response = transport_->send(
            HttpRequest{HttpMethod::Delete, strip_leading_slash(path_with_id), std::nullopt});
        if (!response.ok()) {
            throw std::runtime_error("REST DELETE " + std::string(path_with_id) + " -> HTTP " +
                                     std::to_string(response.status_code));
        }
    }

    static auto strip_leading_slash(std::string_view path) -> std::string {
        std::string result(path);
        if (!result.empty() && result.front() == '/') {
            result.erase(result.begin());
        }
        return result;
    }

    [[nodiscard]] auto suspended_entries() const -> nlohmann::json {
        return raw_get("/ip/firewall/address-list?list=" + disable_log_ + "&address=" + ip_.value());
    }

    [[nodiscard]] auto queue_for_ip() const -> nlohmann::json {
        std::string target = ip_.value() + "%2F32";
        return raw_get("/queue/simple?target=" + target);
    }

    [[nodiscard]] auto filter_rules() const -> nlohmann::json {
        return raw_get("/ip/firewall/filter");
    }

    void cleanup_address_list(const IPAddress& ip) {
        for (const auto& entry : raw_get("/ip/firewall/address-list?list=" + disable_log_)) {
            const std::string address = entry.value("address", "");
            if (address == ip.value()) {
                raw_delete("/ip/firewall/address-list/" + entry.at(".id").get<std::string>());
            }
        }
    }

    void cleanup_speed_queue(const IPAddress& ip) {
        for (const auto& entry : raw_get("/queue/simple")) {
            const std::string target = entry.value("target", "");
            if (target == ip.value() + "/32") {
                raw_delete("/queue/simple/" + entry.at(".id").get<std::string>());
            }
        }
    }

    void cleanup_blocking_rule() {
        for (const auto& entry : filter_rules()) {
            if (entry.value("comment", "") == blocking_comment_) {
                raw_delete("/ip/firewall/filter/" + entry.at(".id").get<std::string>());
            }
        }
    }

    std::unique_ptr<http::HttpClient> transport_;
    std::unique_ptr<MikrotikRouterGateway> gateway_;

    std::string disable_log_;
    std::string queue_prefix_;
    std::string blocking_comment_;
    IPAddress ip_{"10.99.0.10"};
    ContractId contract_{"ct-e2e-1"};
};

TEST_F(MikrotikLiveGatewayTest, DisableThenEnableRemovesSuspensionEntry) {
    gateway_->disableUser(contract_, ip_);

    auto suspended = suspended_entries();
    ASSERT_EQ(suspended.size(), 1);
    EXPECT_EQ(suspended[0].at("list").get<std::string>(), disable_log_);
    EXPECT_EQ(suspended[0].at("address").get<std::string>(), ip_.value());
    EXPECT_NE(suspended[0].value("comment", "").find("ct-e2e-1"), std::string::npos);

    gateway_->enableUser(contract_, ip_);
    EXPECT_TRUE(suspended_entries().empty());
}

TEST_F(MikrotikLiveGatewayTest, ChangeSpeedProfileAppliesMaxLimit) {
    gateway_->changeSpeedProfile(contract_, ip_, SpeedProfile(300, 150));

    auto queue = queue_for_ip();
    ASSERT_EQ(queue.size(), 1);
    EXPECT_EQ(queue[0].at("max-limit").get<std::string>(), "300000000/150000000");
    EXPECT_EQ(queue[0].at("limit-at").get<std::string>(), "300000000/150000000");

    gateway_->changeSpeedProfile(contract_, ip_, SpeedProfile(50, 25));
    queue = queue_for_ip();
    ASSERT_EQ(queue.size(), 1);
    EXPECT_EQ(queue[0].at("max-limit").get<std::string>(), "50000000/25000000");
    EXPECT_EQ(queue[0].value("comment", ""), "inerxia:ct-e2e-1");
    EXPECT_EQ(queue[0].value("name", ""), queue_prefix_ + "ct-e2e-1");
}

TEST_F(MikrotikLiveGatewayTest, BlockingRuleIsCreatedOnceAndIdempotent) {
    auto drop_rules = [this] {
        nlohmann::json matches = nlohmann::json::array();
        for (const auto& rule : filter_rules()) {
            if (rule.value("chain", "") == "forward" &&
                rule.value("action", "") == "drop" &&
                rule.value("src-address-list", "") == disable_log_) {
                matches.push_back(rule);
            }
        }
        return matches;
    };

    ASSERT_EQ(drop_rules().size(), 1);
    gateway_->ensure_blocking_rule();
    EXPECT_EQ(drop_rules().size(), 1);
}

TEST_F(MikrotikLiveGatewayTest, SpeedProfileQueueUsesSlashedTarget) {
    gateway_->changeSpeedProfile(contract_, ip_, SpeedProfile(100, 50));
    ASSERT_EQ(queue_for_ip().size(), 1);
    EXPECT_EQ(queue_for_ip()[0].at("target").get<std::string>(), ip_.value() + "/32");
}

}  // namespace
}  // namespace inerxia::infrastructure::http