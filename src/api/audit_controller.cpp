#include "api/audit_controller.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "api/json_utils.hpp"

namespace inerxia::api {

AuditController::AuditController(application::AuditRepository& audit) : audit_(audit) {}

void AuditController::register_routes(httplib::Server& server) {
    server.Get("/api/audit", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list(req, res);
    });
}

void AuditController::handle_list(const httplib::Request& request,
                                  httplib::Response& response) const {
    run_and_handle(
        [&] {
            std::size_t limit = 50;
            if (const auto raw = request.get_param_value("limit"); !raw.empty()) {
                char* end = nullptr;
                const unsigned long parsed = std::strtoul(raw.c_str(), &end, 10);
                if (end == nullptr || *end != '\0' || parsed == 0) {
                    throw HttpRequestError{400, "bad_request", "Invalid limit parameter"};
                }
                limit = static_cast<std::size_t>(parsed);
            }
            limit = std::min<std::size_t>(limit, 500);

            nlohmann::json body = nlohmann::json::array();
            for (const auto& entry : audit_.list_recent(limit)) {
                body.push_back(nlohmann::json{{"id", entry.id},
                                              {"occurred_at", entry.occurred_at},
                                              {"actor", entry.actor},
                                              {"method", entry.method},
                                              {"path", entry.path},
                                              {"status", entry.status},
                                              {"detail", entry.detail}});
            }
            response.status = 200;
            response.set_content(body.dump(), "application/json");
        },
        response);
}

}  // namespace inerxia::api