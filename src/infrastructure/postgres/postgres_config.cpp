#include "infrastructure/postgres/postgres_config.hpp"

#include "infrastructure/env.hpp"

namespace inerxia::infrastructure::postgres {

PostgresConfig PostgresConfig::from_env() {
    require_environment_variables({"PGDATABASE", "PGUSER", "PGPASSWORD"});
    PostgresConfig config;
    config.host = env_value_or("PGHOST", config.host);
    config.port = env_positive_int_or("PGPORT", config.port);
    config.dbname = env_value_required("PGDATABASE");
    config.user = env_value_required("PGUSER");
    config.password = env_value_required("PGPASSWORD");
    config.sslmode = env_value_or("PGSSLMODE", config.sslmode);
    return config;
}

std::string PostgresConfig::connection_string() const {
    return "host=" + host + " port=" + std::to_string(port) +
           " dbname=" + dbname + " user=" + user + " sslmode=" + sslmode +
           " password=" + password;
}

std::string PostgresConfig::redacted_connection_string() const {
    (void)connection_string();
    return "host=" + host + " port=" + std::to_string(port) + " dbname=" + dbname +
           " user=" + user + " sslmode=" + sslmode + " password=***";
}

}  // namespace inerxia::infrastructure::postgres