#pragma once

#include <httplib.h>

#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::api {

// GET /api/health: infrastructure liveness probe (PostgreSQL reachability).
// Pure infrastructure concern — no business logic.
class HealthController {
public:
    explicit HealthController(infrastructure::postgres::PostgresPool& pool);

    void register_routes(httplib::Server& server);

private:
    void handle_health(const httplib::Request&, httplib::Response&) const;

    infrastructure::postgres::PostgresPool& pool_;
};

}  // namespace inerxia::api