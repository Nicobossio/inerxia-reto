#include "infrastructure/mikrotik/MikrotikEnvConfig.h"

#include "infrastructure/env.hpp"

namespace inerxia::infrastructure {

MikrotikEnvConfig MikrotikEnvConfig::from_env() {
    MikrotikEnvConfig config;
    require_environment_variables({"MIKROTIK_BASE_URL", "MIKROTIK_USER",
                                   "MIKROTIK_PASSWORD"});
    config.base_url = env_value_required("MIKROTIK_BASE_URL");
    config.username = env_value_required("MIKROTIK_USER");
    config.password = env_value_required("MIKROTIK_PASSWORD");

    config.connect_timeout_seconds =
        env_positive_int_or("MIKROTIK_CONNECT_TIMEOUT_SECONDS",
                            config.connect_timeout_seconds);
    config.request_timeout_seconds =
        env_positive_int_or("MIKROTIK_TIMEOUT_SECONDS", config.request_timeout_seconds);
    config.verify_tls = env_bool_or("MIKROTIK_VERIFY_TLS", config.verify_tls);

    config.suspended_list = env_value_or("MIKROTIK_SUSPENDED_LIST", config.suspended_list);
    config.queue_prefix = env_value_or("MIKROTIK_QUEUE_PREFIX", config.queue_prefix);
    config.contract_comment_prefix =
        env_value_or("MIKROTIK_CONTRACT_COMMENT_PREFIX", config.contract_comment_prefix);
    config.blocking_rule_comment =
        env_value_or("MIKROTIK_BLOCKING_RULE_COMMENT", config.blocking_rule_comment);
    return config;
}

http::CurlHttpClientConfig MikrotikEnvConfig::to_curl_config() const noexcept {
    http::CurlHttpClientConfig config;
    config.base_url = base_url;
    config.username = username;
    config.password = password;
    config.connect_timeout_seconds = connect_timeout_seconds;
    config.request_timeout_seconds = request_timeout_seconds;
    config.verify_tls = verify_tls;
    return config;
}

RouterOSConfig MikrotikEnvConfig::to_router_os_config() const noexcept {
    RouterOSConfig config;
    config.suspended_list = suspended_list;
    config.queue_prefix = queue_prefix;
    config.contract_comment_prefix = contract_comment_prefix;
    config.blocking_rule_comment = blocking_rule_comment;
    return config;
}

}  // namespace inerxia::infrastructure