#pragma once

#include <memory>

#include "application/ports/RouterGateway.h"
#include "infrastructure/mikrotik/MikrotikRouterGateway.h"

namespace inerxia::infrastructure {

// Sink used by default when no custom logger is provided: writes sanitized lines
// (method + endpoint + HTTP status, never credentials) to stderr.
RouterLogSink default_router_log_sink();

// Builds the production MikroTik adapter from environment variables:
//   MIKROTIK_BASE_URL (required, e.g. https://router:443/rest)
//   MIKROTIK_USER (required)
//   MIKROTIK_PASSWORD (required)
//   MIKROTIK_CONNECT_TIMEOUT_SECONDS (default 10)
//   MIKROTIK_TIMEOUT_SECONDS (default 30)
//   MIKROTIK_VERIFY_TLS (default true)
// plus the RouterOS naming variables documented in RouterOSConfig::from_env().
//
// The returned gateway owns its HTTP transport; it can be injected wherever
// application::RouterGateway is required.
std::unique_ptr<application::RouterGateway> make_mikrotik_router_gateway_from_env(
    RouterLogSink log = default_router_log_sink());

}  // namespace inerxia::infrastructure