#include "infrastructure/postgres/migrations.hpp"

#include <set>

#include "infrastructure/postgres/pg_row.hpp"

namespace inerxia::infrastructure::postgres {

namespace {

const std::vector<Migration>& migration_scripts() {
    static const std::vector<Migration> scripts = {
        {1, "create_tables",
         R"SQL(
CREATE TABLE internet_plans (
    id                  TEXT PRIMARY KEY,
    name                TEXT NOT NULL CHECK (length(btrim(name)) > 0),
    download_mbps       INTEGER NOT NULL CHECK (download_mbps > 0),
    upload_mbps         INTEGER NOT NULL CHECK (upload_mbps > 0),
    monthly_price_cents BIGINT NOT NULL CHECK (monthly_price_cents > 0)
);

CREATE TABLE subscribers (
    id        TEXT PRIMARY KEY,
    name      TEXT NOT NULL CHECK (length(btrim(name)) > 0),
    static_ip TEXT NOT NULL UNIQUE
);

CREATE TABLE contracts (
    id                     TEXT PRIMARY KEY,
    subscriber_id          TEXT NOT NULL REFERENCES subscribers (id),
    plan_id                TEXT NOT NULL REFERENCES internet_plans (id),
    download_mbps          INTEGER NOT NULL CHECK (download_mbps > 0),
    upload_mbps            INTEGER NOT NULL CHECK (upload_mbps > 0),
    billing_start          DATE NOT NULL,
    due_date               DATE NOT NULL,
    price_per_period_cents BIGINT NOT NULL CHECK (price_per_period_cents > 0),
    suspended_reason       TEXT CHECK (suspended_reason IN ('manual', 'overdue')),
    CHECK (due_date > billing_start)
);

CREATE INDEX idx_contracts_subscriber_id ON contracts (subscriber_id);
CREATE INDEX idx_contracts_due_date ON contracts (due_date);

CREATE TABLE payments (
    id            TEXT PRIMARY KEY,
    contract_id   TEXT NOT NULL REFERENCES contracts (id) ON DELETE CASCADE,
    amount_cents  BIGINT NOT NULL CHECK (amount_cents > 0),
    registered_on DATE NOT NULL
);

CREATE INDEX idx_payments_contract_id ON payments (contract_id);
CREATE INDEX idx_payments_registered_on ON payments (registered_on);
)SQL"},
        {2, "referential_integrity_and_query_indexes",
         R"SQL(
-- Subscribers and plans are referenced by the contracts built on top of them.
-- Deleting either would orphan billing history, so forbid it explicitly instead
-- of relying on the implicit NO ACTION default.
ALTER TABLE contracts
    DROP CONSTRAINT contracts_subscriber_id_fkey,
    ADD CONSTRAINT contracts_subscriber_id_fkey
        FOREIGN KEY (subscriber_id) REFERENCES subscribers (id) ON DELETE RESTRICT;

ALTER TABLE contracts
    DROP CONSTRAINT contracts_plan_id_fkey,
    ADD CONSTRAINT contracts_plan_id_fkey
        FOREIGN KEY (plan_id) REFERENCES internet_plans (id) ON DELETE RESTRICT;

-- The automatic-suspension sweep reads non-suspended contracts past their due
-- date. The partial index covers exactly that row set, so it is smaller than the
-- v1 full index and faster to scan.
DROP INDEX idx_contracts_due_date;
CREATE INDEX idx_contracts_due_date_active
    ON contracts (due_date)
    WHERE suspended_reason IS NULL;

-- Balance calculation fetches one contract's payments ordered by registration
-- date; a single composite index serves both the filter (WHERE contract_id = ?)
-- and the ordering, replacing the v1 single-column index.
DROP INDEX idx_payments_contract_id;
CREATE INDEX idx_payments_contract_id_registered
    ON payments (contract_id, registered_on);
)SQL"},
        {3, "audit_log",
         R"SQL(
CREATE TABLE audit_logs (
    id          BIGSERIAL PRIMARY KEY,
    occurred_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    actor       TEXT,
    method      TEXT NOT NULL,
    path        TEXT NOT NULL,
    status      INTEGER NOT NULL,
    detail      TEXT
);

CREATE INDEX idx_audit_logs_occurred_at ON audit_logs (occurred_at DESC);
)SQL"},
        {4, "users",
         R"SQL(
CREATE TABLE users (
    id            TEXT PRIMARY KEY,
    username      TEXT NOT NULL UNIQUE CHECK (length(btrim(username)) BETWEEN 3 AND 32),
    password_hash TEXT NOT NULL CHECK (length(password_hash) > 0),
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);
)SQL"},
    };
    return scripts;
}

}  // namespace

const std::vector<Migration>& migrations() { return migration_scripts(); }

void apply_migrations(PgConnection& connection) {
    connection.exec(R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version    INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
)
)SQL");

    std::set<int> applied;
    {
        const PgResult result = connection.exec("SELECT version FROM schema_migrations");
        for (int row = 0; row < result.row_count(); ++row) {
            applied.insert(PgRow{result, row}.integer(0));
        }
    }

    for (const auto& migration : migration_scripts()) {
        if (applied.count(migration.version) != 0) {
            continue;
        }
        PgTransaction transaction{connection};
        connection.exec(migration.sql);
        connection.exec_params("INSERT INTO schema_migrations (version, name) VALUES ($1, $2)",
                               {std::to_string(migration.version), migration.name});
        transaction.commit();
    }
}

}  // namespace inerxia::infrastructure::postgres