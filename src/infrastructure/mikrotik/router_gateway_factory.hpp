#pragma once

#include <memory>

#include "application/ports/RouterGateway.h"
#include "infrastructure/mikrotik/MikrotikRouterGateway.h"

namespace inerxia::infrastructure {

// Sink used by default when no custom logger is provided: writes sanitized lines
// (method + endpoint + HTTP status, never credentials) to stderr.
RouterLogSink default_router_log_sink();

// Builds the production MikroTik adapter from environment variables. All variables are
// documented in MikrotikEnvConfig::from_env(); the three transport variables
// (MIKROTIK_BASE_URL, MIKROTIK_USER, MIKROTIK_PASSWORD) are required and validated.
//
// The returned gateway owns its HTTP transport; it can be injected wherever
// application::RouterGateway is required.
std::unique_ptr<application::RouterGateway> make_mikrotik_router_gateway_from_env(
    RouterLogSink log = default_router_log_sink());

}  // namespace inerxia::infrastructure