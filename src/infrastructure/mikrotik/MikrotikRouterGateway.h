#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "application/ports/RouterGateway.h"
#include "domain/ids.hpp"
#include "domain/ip_address.hpp"
#include "domain/speed_profile.hpp"
#include "infrastructure/http/http_client.hpp"

namespace inerxia::infrastructure {

// Log sink used by MikrotikRouterGateway. Receives already sanitized lines:
// method + endpoint + HTTP status, never credentials or request/response bodies.
using RouterLogSink = std::function<void(std::string_view)>;

struct RouterOSConfig {
    std::string suspended_list = "suspended";
    std::string queue_prefix = "inerxia-";
    std::string contract_comment_prefix = "inerxia:";
    std::string blocking_rule_comment = "inerxia:baja-automatica";

    // Reads MIKROTIK_SUSPENDED_LIST, MIKROTIK_QUEUE_PREFIX,
    // MIKROTIK_CONTRACT_COMMENT_PREFIX, MIKROTIK_BLOCKING_RULE_COMMENT.
    // Credentials are never part of this config: they belong to the HTTP transport.
    static RouterOSConfig from_env();
};

class MikrotikRouterGateway final : public application::RouterGateway {
public:
    MikrotikRouterGateway(http::HttpClient&, RouterOSConfig,
                          RouterLogSink log = RouterLogSink{});

    void enableUser(const domain::ContractId&, const domain::IPAddress&) override;
    void disableUser(const domain::ContractId&, const domain::IPAddress&) override;
    void changeSpeedProfile(const domain::ContractId&, const domain::IPAddress&,
                            const domain::SpeedProfile&) override;

    // Provisions the firewall drop rule that enforces suspension. Idempotent.
    void ensure_blocking_rule();

private:
    [[nodiscard]] std::string speed_string(const domain::SpeedProfile&) const;
    [[nodiscard]] std::string suspended_query(const domain::IPAddress&) const;
    [[nodiscard]] std::string queue_path(const domain::IPAddress&) const;
    [[nodiscard]] std::string encode_value(std::string_view) const;
    [[nodiscard]] std::optional<std::string> find_first_id(const http::HttpResponse&) const;
    http::HttpResponse send_or_throw(const http::HttpRequest&,
                                     std::string_view operation) const;
    void log(std::string_view message) const;

    http::HttpClient& http_;
    RouterOSConfig config_;
    RouterLogSink log_;
};

}  // namespace inerxia::infrastructure