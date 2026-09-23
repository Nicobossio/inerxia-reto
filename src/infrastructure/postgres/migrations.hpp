#pragma once

#include <string>
#include <vector>

#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

struct Migration {
    int version;
    std::string name;
    std::string sql;
};

// Ordered list of schema migrations. DDL lives in the infrastructure layer only;
// the domain never sees SQL.
[[nodiscard]] const std::vector<Migration>& migrations();

// Applies every pending migration inside its own transaction and records it in
// schema_migrations, so repeated calls are a no-op. Throws PostgresError on
// failure (rolled back, nothing partially applied).
void apply_migrations(PgConnection& connection);

}  // namespace inerxia::infrastructure::postgres