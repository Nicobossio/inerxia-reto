#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "api/api_server.hpp"
#include "infrastructure/mikrotik/router_gateway_factory.hpp"
#include "infrastructure/postgres/migrations.hpp"
#include "infrastructure/postgres/postgres_config.hpp"
#include "infrastructure/system_time_provider.hpp"

namespace inerxia::api {
namespace {

// API_HOST / API_PORT are optional; the transport leaves them unset.
std::string host_from_env() {
    const char* host = std::getenv("API_HOST");
    return host != nullptr ? std::string{host} : std::string{"127.0.0.1"};
}

int port_from_env() {
    const char* port = std::getenv("API_PORT");
    if (port == nullptr || *port == '\0') {
        return 8484;
    }
    return std::atoi(port);
}

}  // namespace
}  // namespace inerxia::api

int main() {
    try {
        const auto config = inerxia::infrastructure::postgres::PostgresConfig::from_env();
        inerxia::infrastructure::postgres::PostgresPool pool{config};
        {
            auto connection = pool.acquire();
            inerxia::infrastructure::postgres::apply_migrations(*connection);
        }

        auto router = inerxia::infrastructure::make_mikrotik_router_gateway_from_env();
        inerxia::infrastructure::SystemTimeProvider clock;
        inerxia::api::AppServices services{pool, *router, clock};
        inerxia::api::ApiServer server{services, pool};

        const auto host = inerxia::api::host_from_env();
        const int port = inerxia::api::port_from_env();
        std::cout << "Inerxia API listening on http://" << host << ":" << port << '\n';
        return server.listen(host, port) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Failed to start Inerxia API server: " << error.what() << '\n';
        return 1;
    }
}