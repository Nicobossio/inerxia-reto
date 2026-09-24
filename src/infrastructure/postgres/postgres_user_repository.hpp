#pragma once

#include <optional>
#include <string>

#include "application/ports/user_repository.hpp"
#include "infrastructure/postgres/pg_connection.hpp"
#include "infrastructure/postgres/postgres_config.hpp"

namespace inerxia::infrastructure::postgres {

// PostgreSQL adapter for operator accounts (users table, migration v4). A save
// that hits the unique-username constraint is translated into
// application::UsernameAlreadyRegisteredError.
class PostgresUserRepository final : public application::UserRepository {
public:
    explicit PostgresUserRepository(PostgresPool& pool);

    std::string next_id() override;
    std::optional<application::User> find_by_username(const std::string& username) const override;
    void save(const application::User& user) override;

private:
    PostgresPool& pool_;
};

}  // namespace inerxia::infrastructure::postgres