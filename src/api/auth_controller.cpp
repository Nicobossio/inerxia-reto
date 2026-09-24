#include "api/auth_controller.hpp"

#include <string>

#include "api/json_utils.hpp"

namespace inerxia::api {

AuthController::AuthController(application::AuthService& auth) : auth_(auth) {}

void AuthController::register_routes(httplib::Server& server) {
    server.Post("/api/auth/register", [this](const httplib::Request& req,
                                             httplib::Response& res) {
        handle_register(req, res);
    });
    server.Post("/api/auth/login", [this](const httplib::Request& req, httplib::Response& res) {
        handle_login(req, res);
    });
    server.Post("/api/auth/logout", [this](const httplib::Request& req, httplib::Response& res) {
        handle_logout(req, res);
    });
    server.Get("/api/auth/me", [this](const httplib::Request& req, httplib::Response& res) {
        handle_session(req, res);
    });
}

void AuthController::handle_register(const httplib::Request& request,
                                     httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const std::string username = body.at("username").get<std::string>();
            const std::string password = body.at("password").get<std::string>();
            const std::string registered = auth_.register_user(username, password);
            response.status = 201;
            response.set_content(nlohmann::json{{"username", registered}}.dump(),
                                 "application/json");
        },
        response);
}

void AuthController::handle_login(const httplib::Request& request,
                                  httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const std::string username = body.at("username").get<std::string>();
            const std::string password = body.at("password").get<std::string>();
            const auto token = auth_.login(username, password);
            if (!token) {
                reply_error(response, 401, "unauthorized", "Invalid username or password");
                return;
            }
            response.status = 200;
            response.set_content(
                nlohmann::json{{"token", *token}, {"username", username}}.dump(),
                "application/json");
        },
        response);
}

void AuthController::handle_logout(const httplib::Request& request,
                                   httplib::Response& response) const {
    run_and_handle(
        [&] {
            const std::string token = bearer_token(request);
            if (!token.empty()) {
                auth_.logout(token);
            }
            response.status = 204;
        },
        response);
}

void AuthController::handle_session(const httplib::Request& request,
                                    httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto username = auth_.authenticate(bearer_token(request));
            if (!username) {
                reply_error(response, 401, "unauthorized",
                            "Missing or invalid operator session");
                return;
            }
            response.status = 200;
            response.set_content(
                nlohmann::json{{"authenticated", true}, {"username", *username}}.dump(),
                "application/json");
        },
        response);
}

}  // namespace inerxia::api