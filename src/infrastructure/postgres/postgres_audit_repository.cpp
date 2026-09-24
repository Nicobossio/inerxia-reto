#include "infrastructure/postgres/postgres_audit_repository.hpp"

#include <string>

#include "infrastructure/postgres/pg_row.hpp"

namespace inerxia::infrastructure::postgres {

void PostgresAuditRepository::append(const application::AuditEntry& entry) {
    auto connection = pool_.acquire();
    connection->prepare("audit_insert",
                        "INSERT INTO audit_logs (occurred_at, actor, method, path, status, detail) "
                        "VALUES ($1::timestamptz, $2, $3, $4, $5, $6)");
    connection->exec_prepared("audit_insert",
                              {entry.occurred_at, entry.actor, entry.method, entry.path,
                               std::to_string(entry.status), entry.detail});
}

std::vector<application::AuditEntry> PostgresAuditRepository::list_recent(
    std::size_t limit) const {
    auto connection = pool_.acquire();
    connection->prepare(
        "audit_list_recent",
        "SELECT id, to_char(occurred_at AT TIME ZONE 'UTC', "
        "'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS occurred_at, actor, method, path, status, detail "
        "FROM audit_logs ORDER BY id DESC LIMIT $1");
    const PgResult result =
        connection->exec_prepared("audit_list_recent", {std::to_string(limit)});
    std::vector<application::AuditEntry> entries;
    entries.reserve(static_cast<std::size_t>(result.row_count()));
    for (int row = 0; row < result.row_count(); ++row) {
        const PgRow r{result, row};
        application::AuditEntry entry;
        entry.id = r.required_text(0);
        entry.occurred_at = r.required_text(1);
        entry.actor = r.optional_text(2).value_or("anonymous");
        entry.method = r.required_text(3);
        entry.path = r.required_text(4);
        entry.status = r.integer(5);
        entry.detail = r.optional_text(6).value_or("");
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace inerxia::infrastructure::postgres