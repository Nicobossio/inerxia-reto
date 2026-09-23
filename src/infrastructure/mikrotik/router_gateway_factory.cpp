#include "infrastructure/mikrotik/router_gateway_factory.hpp"

#include <cstdio>
#include <memory>

#include "infrastructure/http/curl_http_client.hpp"

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
    auto http = std::make_unique<http::CurlHttpClient>(http::CurlHttpClientConfig::from_env());
    auto gateway = std::make_unique<MikrotikRouterGateway>(
        *http, RouterOSConfig::from_env(), std::move(log));
    return std::make_unique<OwnedRouterGateway>(std::move(http), std::move(gateway));
}

}  // namespace inerxia::infrastructure