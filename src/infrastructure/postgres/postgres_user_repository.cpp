#include "infrastructure/postgres/postgres_user_repository.hpp"

#include <string_view>

#include "application/application_error.hpp"
#include "infrastructure/postgres/pg_row.hpp"
#include "infrastructure/postgres/pg_utils.hpp"

namespace inerxia::infrastructure::postgres {

PostgresUserRepository::PostgresUserRepository(PostgresPool& pool) : pool_(pool) {}

std::string PostgresUserRepository::next_id() {
    return next_uuid_v4();
}

std::optional<application::User> PostgresUserRepository::find_by_username(
    const std::string& username) const {
    auto connection = pool_.acquire();
    connection->prepare("user_select_by_username",
                        "SELECT id, username, password_hash, "
                        "to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
                        "FROM users WHERE username = $1");
    const PgResult result = connection->exec_prepared("user_select_by_username", {username});
    if (result.row_count() == 0) {
        return std::nullopt;
    }
    const PgRow row{result, 0};
    return application::User{row.required_text(0), row.required_text(1), row.required_text(2),
                             row.required_text(3)};
}

void PostgresUserRepository::save(const application::User& user) {
    auto connection = pool_.acquire();
    connection->prepare("user_insert",
                        "INSERT INTO users (id, username, password_hash) VALUES ($1, $2, $3)");
    try {
        connection->exec_prepared("user_insert",
                                  {user.id, user.username, user.password_hash});
    } catch (const PostgresError& error) {
        if (std::string_view{error.what()}.find("duplicate key") != std::string_view::npos) {
            throw application::UsernameAlreadyRegisteredError(
                "Username already registered: " + user.username);
        }
        throw;
    }
}

}  // namespace inerxia::infrastructure::postgres