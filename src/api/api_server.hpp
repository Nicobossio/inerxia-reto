#pragma once

#include <string>

#include <httplib.h>

#include "api/app_services.hpp"
#include "api/audit_controller.hpp"
#include "api/auth_controller.hpp"
#include "api/contract_controller.hpp"
#include "api/docs_controller.hpp"
#include "api/health_controller.hpp"
#include "api/plan_controller.hpp"
#include "api/subscriber_controller.hpp"
#include "application/ports/audit_repository.hpp"
#include "application/services/auth_service.hpp"
#include "infrastructure/auth/in_memory_session_store.hpp"
#include "infrastructure/auth/openssl_pbkdf2_hasher.hpp"
#include "infrastructure/postgres/postgres_audit_repository.hpp"
#include "infrastructure/postgres/postgres_user_repository.hpp"

namespace inerxia::api {

// Bundles the HTTP surface: owns the httplib server and registers every
// controller's routes against it. Pure adapter — no business logic. It also
// wires the two HTTP-level cross-cutting concerns:
//   * auth gate (pre-routing): every private /api/* route requires a valid OAT
//     token; operators self-register (POST /api/auth/register) or log in
//     (POST /api/auth/login), both public.
//   * change audit (post-routing): each mutating /api/* request is recorded
//     with its actor, method, path and status before the response is written.
class ApiServer {
public:
    ApiServer(AppServices& services, infrastructure::postgres::PostgresPool& pool);

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    // Binds to an ephemeral OS-assigned port (for tests).
    int bind_to_any_port(const std::string& host = "127.0.0.1");
    // Serves from the already-bound socket (call in a worker thread in tests).
    bool listen_after_bind();
    // Binds and serves; returns false on bind/listen failure.
    bool listen(const std::string& host, int port);
    void stop();

private:
    // Returns Handled when the request was short-circuited (auth required).
    httplib::Server::HandlerResponse pre_routing(const httplib::Request&, httplib::Response&);
    void post_routing(const httplib::Request&, httplib::Response&);
    bool is_public_path(const httplib::Request&) const;

    httplib::Server server_;
    infrastructure::postgres::PostgresUserRepository user_repository_;
    infrastructure::auth::OpenSslPbkdf2Hasher password_hasher_;
    infrastructure::auth::InMemorySessionStore session_store_;
    application::AuthService auth_service_;
    infrastructure::postgres::PostgresAuditRepository audit_;
    AuthController auth_controller_;
    AuditController audit_controller_;
    SubscriberController subscriber_controller_;
    PlanController plan_controller_;
    ContractController contract_controller_;
    HealthController health_controller_;
    DocsController docs_controller_;
};

}  // namespace inerxia::api