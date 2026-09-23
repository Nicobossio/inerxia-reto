#pragma once

#include <string>

namespace inerxia::infrastructure::postgres {

// Connection settings for the PostgreSQL adapter. Reads the standard libpq
// environment names so operators can reuse their usual .pgpass/PG* syntax.
struct PostgresConfig {
    std::string host = "127.0.0.1";
    int port = 5432;
    std::string dbname;
    std::string user;
    std::string password;
    std::string sslmode = "disable";

    static PostgresConfig from_env();

    // Full connection string (with password) for PQconnectdbParams-compatible
    // diagnostics. Password IS included; do not log the result.
    [[nodiscard]] std::string connection_string() const;

    // Connection description with the password masked, safe to log.
    [[nodiscard]] std::string redacted_connection_string() const;
};

}  // namespace inerxia::infrastructure::postgres