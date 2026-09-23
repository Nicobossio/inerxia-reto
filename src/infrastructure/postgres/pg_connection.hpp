#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libpq-fe.h>

#include "infrastructure/infrastructure_error.hpp"
#include "infrastructure/postgres/postgres_config.hpp"

namespace inerxia::infrastructure::postgres {

// RAII owner of a PGresult (frees it with PQclear).
class PgResult {
public:
    PgResult() = default;
    explicit PgResult(PGresult* result) : result_(result) {}
    ~PgResult() { reset(nullptr); }

    PgResult(PgResult&& other) noexcept : result_(other.release()) {}
    PgResult& operator=(PgResult&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    PgResult(const PgResult&) = delete;
    PgResult& operator=(const PgResult&) = delete;

    PGresult* get() const noexcept { return result_; }

    [[nodiscard]] bool command_ok() const noexcept {
        return result_ != nullptr && PQresultStatus(result_) == PGRES_COMMAND_OK;
    }
    [[nodiscard]] bool tuples_ok() const noexcept {
        return result_ != nullptr && PQresultStatus(result_) == PGRES_TUPLES_OK;
    }
    [[nodiscard]] bool bad() const noexcept {
        return result_ == nullptr ||
               (PQresultStatus(result_) != PGRES_COMMAND_OK &&
                PQresultStatus(result_) != PGRES_TUPLES_OK);
    }

    [[nodiscard]] int row_count() const { return PQntuples(result_); }
    [[nodiscard]] int field_count() const { return PQnfields(result_); }
    [[nodiscard]] bool is_null(int row, int column) const {
        return PQgetisnull(result_, row, column) != 0;
    }
    [[nodiscard]] std::string_view text(int row, int column) const {
        return PQgetvalue(result_, row, column);
    }

private:
    PGresult* release() noexcept {
        PGresult* held = result_;
        result_ = nullptr;
        return held;
    }

    void reset(PGresult* result) noexcept {
        if (result_ != nullptr) {
            PQclear(result_);
        }
        result_ = result;
    }

    PGresult* result_ = nullptr;
};

// Owns a physical libpq connection. Methods throw PostgresError on failure.
// Prepared statements prepared on this connection are cached (name -> SQL), so
// re-preparing the same statement is a no-op.
class PgConnection {
public:
    explicit PgConnection(const PostgresConfig& config);
    ~PgConnection() noexcept { close(); }

    PgConnection(PgConnection&& other) noexcept
        : conn_(std::exchange(other.conn_, nullptr)),
          prepared_(std::move(other.prepared_)) {}
    PgConnection& operator=(PgConnection&& other) noexcept {
        if (this != &other) {
            close();
            conn_ = std::exchange(other.conn_, nullptr);
            prepared_ = std::move(other.prepared_);
        }
        return *this;
    }
    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;

    PgResult exec(std::string_view sql) const;
    PgResult exec_params(std::string_view sql,
                         const std::vector<std::optional<std::string>>& params) const;

    void prepare(const std::string& name, std::string sql);
    PgResult exec_prepared(const std::string& name,
                           const std::vector<std::optional<std::string>>& params) const;

    void begin() const { exec("BEGIN"); }
    void commit() const { exec("COMMIT"); }
    void rollback() const noexcept {
        try {
            exec("ROLLBACK");
        } catch (const InfrastructureError&) {
            // Connection is probably lost; nothing safe to do at this point.
        }
    }

    [[nodiscard]] bool healthy() const noexcept {
        return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
    }
    void close() noexcept;

private:
    [[nodiscard]] static PgResult checked(PGresult* result, const PgConnection& connection,
                                          const char* what);

    PGconn* conn_ = nullptr;
    std::map<std::string, std::string> prepared_;
};

// Begins a transaction on acquire and rolls back on destruction unless commit()
// was reached, so exceptions unwind to a consistent state.
class PgTransaction {
public:
    explicit PgTransaction(PgConnection& connection) : connection_(connection) {
        connection_.begin();
    }
    ~PgTransaction() noexcept {
        if (!committed_) {
            connection_.rollback();
        }
    }
    PgTransaction(const PgTransaction&) = delete;
    PgTransaction& operator=(const PgTransaction&) = delete;

    void commit() {
        connection_.commit();
        committed_ = true;
    }

private:
    PgConnection& connection_;
    bool committed_ = false;
};

// Small thread-safe pool handing out live connections. Failed connections are
// discarded instead of being recycled.
class PostgresPool {
public:
    // RAII handle: returns the connection to the pool on destruction (unless it
    // is unhealthy, in which case it is closed and discarded).
    class Borrowed {
    public:
        Borrowed() = default;
        Borrowed(PgConnection connection, PostgresPool* pool)
            : connection_(std::move(connection)), pool_(pool) {}
        ~Borrowed() {
            if (pool_ != nullptr && connection_.healthy()) {
                pool_->release(std::move(connection_));
            }
        }
        Borrowed(Borrowed&& other) noexcept
            : connection_(std::move(other.connection_)), pool_(std::exchange(other.pool_, nullptr)) {}
        Borrowed& operator=(Borrowed&& other) noexcept {
            if (this != &other) {
                if (pool_ != nullptr && connection_.healthy()) {
                    pool_->release(std::move(connection_));
                }
                connection_ = std::move(other.connection_);
                pool_ = std::exchange(other.pool_, nullptr);
            }
            return *this;
        }
        Borrowed(const Borrowed&) = delete;
        Borrowed& operator=(const Borrowed&) = delete;

        PgConnection& operator*() noexcept { return connection_; }
        PgConnection* operator->() noexcept { return &connection_; }

    private:
        PgConnection connection_;
        PostgresPool* pool_ = nullptr;
    };

    explicit PostgresPool(PostgresConfig config, std::size_t max_total = 8);
    ~PostgresPool() { close(); }

    PostgresPool(const PostgresPool&) = delete;
    PostgresPool& operator=(const PostgresPool&) = delete;

    Borrowed acquire();
    void release(PgConnection connection) noexcept;
    void close() noexcept;

private:
    PostgresConfig config_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<PgConnection> idle_;
    std::size_t total_ = 0;
    const std::size_t max_total_;
    bool closed_ = false;
};

}  // namespace inerxia::infrastructure::postgres