#pragma once

#include <string>

#include <httplib.h>

#include "api/app_services.hpp"
#include "api/contract_controller.hpp"
#include "api/docs_controller.hpp"
#include "api/health_controller.hpp"
#include "api/plan_controller.hpp"
#include "api/subscriber_controller.hpp"

namespace inerxia::api {

// Bundles the HTTP surface: owns the httplib server and registers every
// controller's routes against it. Pure adapter — no business logic.
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
    httplib::Server server_;
    SubscriberController subscriber_controller_;
    PlanController plan_controller_;
    ContractController contract_controller_;
    HealthController health_controller_;
    DocsController docs_controller_;
};

}  // namespace inerxia::api