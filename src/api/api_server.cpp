#include "api/api_server.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "api/json_utils.hpp"

namespace inerxia::api {

namespace {

constexpr std::string_view kActorHeader = "X-Inerxia-Actor";

std::chrono::milliseconds session_ttl_from_env() {
    const char* raw = std::getenv("AUTH_SESSION_TTL_MS");
    if (raw != nullptr) {
        char* end = nullptr;
        const long parsed = std::strtol(raw, &end, 10);
        if (end != nullptr && *end == '\0' && parsed > 0) {
            return std::chrono::milliseconds{parsed};
        }
    }
    return std::chrono::hours{12};
}

// Compresses a request body into a short single-line description for the audit
// log. Deliberately excludes the auth routes (handled separately) so secrets —
// passwords from login/register — are never persisted.
std::string audit_detail(const httplib::Request& request) {
    if (request.path == "/api/auth/login") {
        return request.method == "POST" ? "user login" : "login attempt";
    }
    if (request.path == "/api/auth/register") {
        return "user registration";
    }
    if (request.path == "/api/auth/logout") {
        return "user logout";
    }
    std::string sanitized;
    sanitized.reserve(request.body.size());
    for (const char c : request.body) {
        sanitized.push_back((c == '\n' || c == '\r' || c == '\t') ? ' ' : c);
    }
    if (sanitized.size() > 200) {
        sanitized.resize(200);
    }
    return sanitized;
}

}  // namespace

ApiServer::ApiServer(AppServices& services, infrastructure::postgres::PostgresPool& pool)
    : user_repository_{pool},
      auth_service_{user_repository_, password_hasher_, session_store_,
                    session_ttl_from_env()},
      audit_{pool},
      auth_controller_{auth_service_},
      audit_controller_{audit_},
      subscriber_controller_{services.create_subscriber, services.get_subscriber,
                             services.list_subscribers},
      plan_controller_{services.create_plan, services.get_plan},
      contract_controller_{services.create_contract,
                           services.get_contract,
                           services.update_contract,
                           services.suspend_contract,
                           services.reactivate_contract,
                           services.change_speed_profile,
                           services.register_payment,
                           services.evaluate_expired_contracts},
      health_controller_{pool} {
    server_.set_pre_routing_handler([this](const httplib::Request& request,
                                           httplib::Response& response) {
        return pre_routing(request, response);
    });
    server_.set_post_routing_handler([this](const httplib::Request& request,
                                            httplib::Response& response) {
        post_routing(request, response);
    });

    auth_controller_.register_routes(server_);
    audit_controller_.register_routes(server_);
    subscriber_controller_.register_routes(server_);
    plan_controller_.register_routes(server_);
    contract_controller_.register_routes(server_);
    health_controller_.register_routes(server_);
    docs_controller_.register_routes(server_);
}

int ApiServer::bind_to_any_port(const std::string& host) {
    return server_.bind_to_any_port(host);
}

bool ApiServer::listen_after_bind() {
    return server_.listen_after_bind();
}

bool ApiServer::listen(const std::string& host, int port) {
    return server_.listen(host, port);
}

void ApiServer::stop() {
    server_.stop();
}

httplib::Server::HandlerResponse ApiServer::pre_routing(const httplib::Request& request,
                                                        httplib::Response& response) {
    response.set_header(std::string{kActorHeader}, "anonymous");
    if (is_public_path(request)) {
        return httplib::Server::HandlerResponse::Unhandled;
    }
    const auto username = auth_service_.authenticate(bearer_token(request));
    if (!username) {
        reply_error(response, 401, "unauthorized",
                    "Missing or invalid operator session");
        return httplib::Server::HandlerResponse::Handled;
    }
    response.set_header(std::string{kActorHeader}, *username);
    return httplib::Server::HandlerResponse::Unhandled;
}

bool ApiServer::is_public_path(const httplib::Request& request) const {
    if (request.path.rfind("/api/", 0) != 0) {
        return true;  // dashboard, swagger, openapi spec, root.
    }
    return request.path == "/api/openapi.json" || request.path == "/api/health" ||
           request.path == "/api/auth/register" || request.path == "/api/auth/login" ||
           request.path == "/api/auth/logout";
}

void ApiServer::post_routing(const httplib::Request& request, httplib::Response& response) {
    const std::string actor = response.get_header_value(std::string{kActorHeader}, "anonymous");
    // Internal header: it must never reach the client.
    response.headers.erase(std::string{kActorHeader});

    if (request.path.rfind("/api/", 0) != 0) {
        return;
    }
    const bool mutating = request.method == "POST" || request.method == "PUT" ||
                          request.method == "PATCH" || request.method == "DELETE";
    if (!mutating) {
        return;
    }

    application::AuditEntry entry;
    entry.occurred_at = iso_utc_now();
    entry.actor = actor;
    entry.method = request.method;
    entry.path = request.path;
    entry.status = response.status;
    entry.detail = audit_detail(request);
    try {
        audit_.append(entry);
    } catch (const std::exception& error) {
        // Audit must never break the response already generated by the handler.
        std::fprintf(stderr, "[audit] failed to record %s %s: %s\n",
                     request.method.c_str(), request.path.c_str(), error.what());
    }
}

}  // namespace inerxia::api