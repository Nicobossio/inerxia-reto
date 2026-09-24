#pragma once

#include <httplib.h>

#include "application/services/auth_service.hpp"

namespace inerxia::api {

// HTTP adapter for the operator session endpoints (register/login/logout/
// session check). Only translates HTTP to the AuthService use cases; no
// credential- or registration-policy logic lives here.
class AuthController {
public:
    explicit AuthController(application::AuthService& auth);

    void register_routes(httplib::Server& server);

private:
    void handle_register(const httplib::Request&, httplib::Response&) const;
    void handle_login(const httplib::Request&, httplib::Response&) const;
    void handle_logout(const httplib::Request&, httplib::Response&) const;
    void handle_session(const httplib::Request&, httplib::Response&) const;

    application::AuthService& auth_;
};

}  // namespace inerxia::api