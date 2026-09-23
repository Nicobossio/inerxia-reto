#include "api/health_controller.hpp"

#include "api/json_utils.hpp"
#include "nlohmann/json.hpp"

namespace inerxia::api {

HealthController::HealthController(infrastructure::postgres::PostgresPool& pool)
    : pool_(pool) {}

void HealthController::register_routes(httplib::Server& server) {
    server.Get("/api/health", [this](const httplib::Request& req, httplib::Response& res) {
        handle_health(req, res);
    });
}

void HealthController::handle_health(const httplib::Request&, httplib::Response& response) const {
    run_and_handle(
        [&] {
            // Fails with infra::PostgresError (-> 503) when the database is down.
            auto connection = pool_.acquire();
            (void)connection;
            response.status = 200;
            response.set_content(nlohmann::json{{"status", "ok"}}.dump(),
                                 "application/json");
        },
        response);
}

}  // namespace inerxia::api