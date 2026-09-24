#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "api/api_server.hpp"
#include "infrastructure/mikrotik/router_gateway_factory.hpp"
#include "infrastructure/postgres/migrations.hpp"
#include "infrastructure/postgres/postgres_config.hpp"
#include "infrastructure/scheduler/periodic_scheduler.hpp"
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

// SWEEP_INTERVAL_MS governs how often the overdue-contract sweep runs.
//   * unset   -> 60000 ms (default)
//   * 0       -> scheduler disabled (manual POST /api/sweep/expired still works)
//   * invalid -> default
std::chrono::milliseconds sweep_interval_from_env() {
    const char* value = std::getenv("SWEEP_INTERVAL_MS");
    if (value == nullptr || *value == '\0') {
        return std::chrono::milliseconds{60'000};
    }
    const long ms = std::atol(value);
    if (ms <= 0) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::milliseconds{ms};
}

// Set by the SIGINT/SIGTERM handlers; the watchdog thread turns it into a
// graceful server + scheduler shutdown (no blocking of the HTTP thread).
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int) {
    g_shutdown_requested.store(true);
}

}  // namespace
}  // namespace inerxia::api

int main() {
    using namespace inerxia::api;
    try {
        const auto config = inerxia::infrastructure::postgres::PostgresConfig::from_env();
        inerxia::infrastructure::postgres::PostgresPool pool{config};
        {
            auto connection = pool.acquire();
            inerxia::infrastructure::postgres::apply_migrations(*connection);
        }

        auto router = inerxia::infrastructure::make_mikrotik_router_gateway_from_env();
        inerxia::infrastructure::SystemTimeProvider clock;
        AppServices services{pool, *router, clock};
        ApiServer server{services, pool};

        const auto sweep_interval = sweep_interval_from_env();
        inerxia::infrastructure::PeriodicScheduler sweep{
            sweep_interval,
            [&services] {
                const auto suspended = services.evaluate_expired_contracts();
                std::cout << "[sweep] suspended " << suspended.size()
                          << " overdue contract(s)\n";
            }};
        if (sweep_interval.count() > 0) {
            sweep.start();
        } else {
            std::cout << "[sweep] automatic sweep disabled (SWEEP_INTERVAL_MS=0); "
                         "manual POST /api/sweep/expired still available\n";
        }

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        const auto host = host_from_env();
        const int port = port_from_env();
        std::cout << "Inerxia API listening on http://" << host << ":" << port << '\n';

        // Watchdog: turns a signal flag into server.stop() + sweep.stop(). Runs
        // on its own thread so the HTTP thread is never blocked.
        std::thread shutdown_watchdog{[&] {
            while (!g_shutdown_requested.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            server.stop();
            sweep.stop();
        }};

        const bool served = server.listen(host, port);
        // Wake the watchdog even when listen() aborts early (bind failure) so
        // join() does not hang.
        g_shutdown_requested.store(true);
        sweep.stop();
        server.stop();
        shutdown_watchdog.join();
        return served ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Failed to start Inerxia API server: " << error.what() << '\n';
        return 1;
    }
}