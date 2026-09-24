#include "infrastructure/mikrotik/router_gateway_factory.hpp"

#include <cstdio>
#include <memory>

#include "infrastructure/http/curl_http_client.hpp"
#include "infrastructure/mikrotik/MikrotikEnvConfig.h"
#include "infrastructure/mikrotik/retrying_router_gateway.hpp"

namespace inerxia::infrastructure {
namespace {

class OwnedRouterGateway final : public application::RouterGateway {
public:
    OwnedRouterGateway(std::unique_ptr<http::HttpClient> http,
                       std::unique_ptr<MikrotikRouterGateway> gateway)
        : http_(std::move(http)), gateway_(std::move(gateway)) {}

    void enableUser(const domain::ContractId& contract_id,
                    const domain::IPAddress& ip) override {
        gateway_->enableUser(contract_id, ip);
    }

    void disableUser(const domain::ContractId& contract_id,
                     const domain::IPAddress& ip) override {
        gateway_->disableUser(contract_id, ip);
    }

    void changeSpeedProfile(const domain::ContractId& contract_id,
                            const domain::IPAddress& ip,
                            const domain::SpeedProfile& profile) override {
        gateway_->changeSpeedProfile(contract_id, ip, profile);
    }

private:
    std::unique_ptr<http::HttpClient> http_;
    std::unique_ptr<MikrotikRouterGateway> gateway_;
};

}  // namespace

RouterLogSink default_router_log_sink() {
    return [](std::string_view line) {
        std::fprintf(stderr, "[mikrotik] %.*s\n", static_cast<int>(line.size()), line.data());
    };
}

std::unique_ptr<application::RouterGateway> make_mikrotik_router_gateway_from_env(
    RouterLogSink log) {
    const auto env = MikrotikEnvConfig::from_env();
    auto http = std::make_unique<http::CurlHttpClient>(env.to_curl_config());
    auto gateway =
        std::make_unique<MikrotikRouterGateway>(*http, env.to_router_os_config(),
                                                std::move(log));
    auto owner = std::make_unique<OwnedRouterGateway>(std::move(http), std::move(gateway));
    // Wrap every RouterOS call in a bounded retry loop (MIKROTIK_RETRY_* env).
    // Transient failures are retried with backoff; permanent ones fail fast. All
    // RouterGateway operations are idempotent, so retries never duplicate effects.
    RouterLogSink retry_sink = [](std::string_view line) {
        std::fprintf(stderr, "[mikrotik-retry] %.*s\n", static_cast<int>(line.size()),
                     line.data());
    };
    return std::make_unique<RetryingRouterGateway>(std::move(owner), RetryPolicy::from_env(),
                                                   std::move(retry_sink));
}

}  // namespace inerxia::infrastructure