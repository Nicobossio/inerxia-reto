#include "api/api_server.hpp"

namespace inerxia::api {

ApiServer::ApiServer(AppServices& services, infrastructure::postgres::PostgresPool& pool)
    : subscriber_controller_{services.create_subscriber, services.get_subscriber},
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

}  // namespace inerxia::api