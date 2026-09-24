#include "infrastructure/mikrotik/MikrotikRouterGateway.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <optional>
#include <string>

#include "infrastructure/env.hpp"
#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::infrastructure {

using json = nlohmann::json;

namespace {

std::string as_string(const json& value) {
    return value.is_string() ? value.get<std::string>() : std::string{};
}

const char* method_as_string(http::HttpMethod method) {
    switch (method) {
        case http::HttpMethod::Get:
            return "GET";
        case http::HttpMethod::Put:
            return "PUT";
        case http::HttpMethod::Patch:
            return "PATCH";
        case http::HttpMethod::Delete:
            return "DELETE";
    }
    return "GET";
}

}  // namespace

RouterOSConfig RouterOSConfig::from_env() {
    RouterOSConfig config;
    config.suspended_list = env_value_or("MIKROTIK_SUSPENDED_LIST", config.suspended_list);
    config.queue_prefix = env_value_or("MIKROTIK_QUEUE_PREFIX", config.queue_prefix);
    config.contract_comment_prefix =
        env_value_or("MIKROTIK_CONTRACT_COMMENT_PREFIX", config.contract_comment_prefix);
    config.blocking_rule_comment =
        env_value_or("MIKROTIK_BLOCKING_RULE_COMMENT", config.blocking_rule_comment);
    return config;
}

MikrotikRouterGateway::MikrotikRouterGateway(http::HttpClient& http, RouterOSConfig config,
                                             RouterLogSink log)
    : http_(http), config_(std::move(config)), log_(std::move(log)) {}

void MikrotikRouterGateway::log(std::string_view message) const {
    if (log_) {
        log_(message);
    }
}

std::string MikrotikRouterGateway::speed_string(const domain::SpeedProfile& profile) const {
    return std::to_string(profile.download_mbps()) + "M/" +
           std::to_string(profile.upload_mbps()) + "M";
}

// RouterOS reports rates as plain bps ("300000000/150000000"), so comparisons
// against the wire values must be done in bps too, not in "300M/150M" form.
std::string MikrotikRouterGateway::speed_bps_string(const domain::SpeedProfile& profile) const {
    return std::to_string(profile.download_mbps() * 1'000'000) + "/" +
           std::to_string(profile.upload_mbps() * 1'000'000);
}

std::string MikrotikRouterGateway::encode_value(std::string_view value) const {
    constexpr std::string_view kUnreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string encoded;
    encoded.reserve(value.size());
    for (const char c : value) {
        if (kUnreserved.find(c) != std::string_view::npos) {
            encoded.push_back(c);
        } else {
            const auto hex = "0123456789ABCDEF";
            encoded.push_back('%');
            encoded.push_back(hex[static_cast<unsigned char>(c) >> 4]);
            encoded.push_back(hex[static_cast<unsigned char>(c) & 0x0F]);
        }
    }
    return encoded;
}

std::string MikrotikRouterGateway::suspended_query(const domain::IPAddress& ip) const {
    return "list=" + encode_value(config_.suspended_list) + "&address=" +
           encode_value(ip.value());
}

std::string MikrotikRouterGateway::queue_path(const domain::IPAddress& ip) const {
    return "queue/simple?target=" + encode_value(ip.value() + "/32");
}

std::optional<std::string> MikrotikRouterGateway::find_first_id(
    const http::HttpResponse& response) const {
    if (!response.body.has_value()) {
        return std::nullopt;
    }
    const json parsed = json::parse(*response.body);
    if (!parsed.is_array() || parsed.empty()) {
        return std::nullopt;
    }
    // RouterOS exposes every resource id under the ".id" property (e.g. "*A").
    const auto id = parsed.front().find(".id");
    if (id == parsed.front().end() || !id->is_string()) {
        return std::nullopt;
    }
    return id->get<std::string>();
}

http::HttpResponse MikrotikRouterGateway::send_or_throw(const http::HttpRequest& request,
                                                        std::string_view operation) const {
    try {
        const auto response = http_.send(request);
        log(std::string("RouterOS ") + method_as_string(request.method) + " " +
            request.path + " -> HTTP " + std::to_string(response.status_code));
        if (!response.ok()) {
            throw RouterOSApiError(std::string(operation) +
                                       ": RouterOS API error (HTTP " +
                                       std::to_string(response.status_code) + ")",
                                   response.status_code);
        }
        return response;
    } catch (const std::exception& error) {
        log(std::string("RouterOS ") + std::string(operation) + " failed: " + error.what());
        throw;
    }
}

void MikrotikRouterGateway::disableUser(const domain::ContractId& contract_id,
                                        const domain::IPAddress& ip) {
    const auto get = send_or_throw(
        {http::HttpMethod::Get, "ip/firewall/address-list?" + suspended_query(ip),
         std::nullopt},
        "disableUser lookup");
    if (find_first_id(get).has_value()) {
        return;
    }

    const json body = {
        {"list", config_.suspended_list},
        {"address", ip.value()},
        {"comment", config_.contract_comment_prefix + std::string(contract_id.value())},
    };
    send_or_throw({http::HttpMethod::Put, "ip/firewall/address-list", body.dump()},
                  "disableUser add-to-list");
}

void MikrotikRouterGateway::enableUser(const domain::ContractId&,
                                       const domain::IPAddress& ip) {
    const auto get = send_or_throw(
        {http::HttpMethod::Get, "ip/firewall/address-list?" + suspended_query(ip),
         std::nullopt},
        "enableUser lookup");
    const auto id = find_first_id(get);
    if (!id.has_value()) {
        return;
    }

    send_or_throw({http::HttpMethod::Delete, "ip/firewall/address-list/" + *id, std::nullopt},
                  "enableUser remove-from-list");
}

void MikrotikRouterGateway::changeSpeedProfile(const domain::ContractId& contract_id,
                                               const domain::IPAddress& ip,
                                               const domain::SpeedProfile& profile) {
    const auto get = send_or_throw({http::HttpMethod::Get, queue_path(ip), std::nullopt},
                                   "changeSpeedProfile lookup");
    const auto id = find_first_id(get);
    const std::string desired = speed_bps_string(profile);
    const std::string rate_limit = speed_string(profile);
    const std::string queue_name = config_.queue_prefix + std::string(contract_id.value());
    const std::string queue_comment =
        config_.contract_comment_prefix + std::string(contract_id.value());

    if (!id.has_value()) {
        const json body = {
            {"name", queue_name},
            {"target", ip.value() + "/32"},
            {"max-limit", rate_limit},
            {"limit-at", rate_limit},
            {"comment", queue_comment},
        };
        send_or_throw({http::HttpMethod::Put, "queue/simple", body.dump()},
                      "changeSpeedProfile create-queue");
        return;
    }

    json existing = json::parse(*get.body);
    const auto& item = existing.front();
    if (as_string(item.value("max-limit", "")) == desired &&
        as_string(item.value("limit-at", "")) == desired) {
        return;
    }

    const json patch = {
        {"max-limit", rate_limit},
        {"limit-at", rate_limit},
    };
    send_or_throw({http::HttpMethod::Patch, "queue/simple/" + *id, patch.dump()},
                  "changeSpeedProfile update-queue");
}

void MikrotikRouterGateway::ensure_blocking_rule() {
    const auto get = send_or_throw(
        {http::HttpMethod::Get,
         "ip/firewall/filter?chain=forward&action=drop&src-address-list=" +
             encode_value(config_.suspended_list),
         std::nullopt},
        "ensure_blocking_rule lookup");
    if (find_first_id(get).has_value()) {
        return;
    }

    const json body = {
        {"chain", "forward"},
        {"action", "drop"},
        {"src-address-list", config_.suspended_list},
        {"comment", config_.blocking_rule_comment},
    };
    send_or_throw({http::HttpMethod::Put, "ip/firewall/filter", body.dump()},
                  "ensure_blocking_rule create-rule");
}

}  // namespace inerxia::infrastructure