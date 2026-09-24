#pragma once

#include <cstddef>
#include <vector>

#include "application/ports/audit_repository.hpp"
#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

class PostgresAuditRepository final : public application::AuditRepository {
public:
    explicit PostgresAuditRepository(PostgresPool& pool) : pool_(pool) {}

    void append(const application::AuditEntry& entry) override;
    std::vector<application::AuditEntry> list_recent(std::size_t limit) const override;

private:
    PostgresPool& pool_;
};

}  // namespace inerxia::infrastructure::postgres