#pragma once

#include <string>

#include "infrastructure/http/curl_http_client.hpp"
#include "infrastructure/mikrotik/MikrotikRouterGateway.h"

namespace inerxia::infrastructure {

// Single, curl-free source of truth for the MikroTik environment configuration.
// Validation happens here, so it is unit-testable without libcurl.
struct MikrotikEnvConfig {
    // Required.
    std::string base_url;       // e.g. "https://router.lan:443/rest"
    std::string username;
    std::string password;

    // Optional.
    int connect_timeout_seconds = 10;
    int request_timeout_seconds = 30;
    bool verify_tls = true;
    std::string suspended_list = "suspended";
    std::string queue_prefix = "inerxia-";
    std::string contract_comment_prefix = "inerxia:";
    std::string blocking_rule_comment = "inerxia:baja-automatica";

    // Reads and validates:
    //   MIKROTIK_BASE_URL, MIKROTIK_USER, MIKROTIK_PASSWORD  (required, non-blank)
    //   MIKROTIK_CONNECT_TIMEOUT_SECONDS (default 10), MIKROTIK_TIMEOUT_SECONDS (default 30)
    //   MIKROTIK_VERIFY_TLS (default true),
    //   MIKROTIK_SUSPENDED_LIST, MIKROTIK_QUEUE_PREFIX, MIKROTIK_CONTRACT_COMMENT_PREFIX,
    //   MIKROTIK_BLOCKING_RULE_COMMENT.
    // Throws InfrastructureError if any required variable is missing or blank, or if an
    // integer variable is not a positive integer.
    static MikrotikEnvConfig from_env();

    [[nodiscard]] http::CurlHttpClientConfig to_curl_config() const noexcept;
    [[nodiscard]] RouterOSConfig to_router_os_config() const noexcept;
};

}  // namespace inerxia::infrastructure