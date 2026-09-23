#include "infrastructure/postgres/pg_connection.hpp"

#include <array>
#include <cctype>
#include <string>

namespace inerxia::infrastructure::postgres {
namespace {

std::string trim_copy(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return std::string{value.substr(first, last - first)};
}

// Builds the parallel arrays libpq needs for text-format parameters.
struct ParamArrays {
    std::vector<const char*> values;
    std::vector<int> lengths;
    std::vector<int> formats;
};

ParamArrays to_param_arrays(const std::vector<std::optional<std::string>>& params) {
    ParamArrays arrays;
    arrays.values.reserve(params.size());
    arrays.lengths.reserve(params.size());
    arrays.formats.reserve(params.size());
    for (const auto& param : params) {
        arrays.values.push_back(param.has_value() ? param->c_str() : nullptr);
        arrays.lengths.push_back(
            param.has_value() ? static_cast<int>(param->size()) : 0);
        arrays.formats.push_back(0);
    }
    return arrays;
}

}  // namespace

PgConnection::PgConnection(const PostgresConfig& config) {
    const auto keywords = std::array<const char*, 9>{
        "host", "port", "dbname", "user", "password", "sslmode", "client_encoding", "application_name", nullptr};
    const auto values = std::array<std::string, 8>{
        config.host,
        std::to_string(config.port),
        config.dbname,
        config.user,
        config.password,
        config.sslmode,
        "UTF8",
        "inerxia"};
    std::array<const char*, 8> value_ptrs{};
    for (std::size_t i = 0; i < value_ptrs.size(); ++i) {
        value_ptrs[i] = values[i].c_str();
    }

    conn_ = PQconnectdbParams(keywords.data(), value_ptrs.data(), 0);
    if (conn_ == nullptr) {
        throw PostgresError("PostgreSQL connection failed (out of memory): " +
                            config.redacted_connection_string());
    }
    if (PQstatus(conn_) != CONNECTION_OK) {
        const std::string detail = trim_copy(PQerrorMessage(conn_));
        close();
        throw PostgresError("PostgreSQL connection failed for " +
                            config.redacted_connection_string() +
                            (detail.empty() ? std::string{} : ": " + detail));
    }
}

void PgConnection::close() noexcept {
    if (conn_ != nullptr) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
    prepared_.clear();
}

PgResult PgConnection::checked(PGresult* result, const PgConnection& connection,
                               const char* what) {
    PgResult wrapped{result};
    if (wrapped.bad()) {
        std::string detail;
        if (result != nullptr) {
            const char* primary = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);
            const char* detail_field = PQresultErrorField(result, PG_DIAG_MESSAGE_DETAIL);
            if (primary != nullptr) {
                detail = primary;
            } else if (detail_field != nullptr) {
                detail = detail_field;
            } else {
                detail = trim_copy(PQerrorMessage(connection.conn_));
            }
        }
        throw PostgresError(std::string("PostgreSQL ") + what + " failed" +
                            (detail.empty() ? std::string{} : ": " + detail));
    }
    return wrapped;
}

PgResult PgConnection::exec(std::string_view sql) const {
    if (conn_ == nullptr) {
        throw PostgresError("PostgreSQL connection is not open");
    }
    return checked(PQexec(conn_, std::string{sql}.c_str()), *this, "exec");
}

PgResult PgConnection::exec_params(
    std::string_view sql, const std::vector<std::optional<std::string>>& params) const {
    if (conn_ == nullptr) {
        throw PostgresError("PostgreSQL connection is not open");
    }
    const auto arrays = to_param_arrays(params);
    return checked(PQexecParams(conn_, std::string{sql}.c_str(),
                                static_cast<int>(params.size()), nullptr,
                                arrays.values.data(), arrays.lengths.data(),
                                arrays.formats.data(), 0),
                   *this, "parameterized exec");
}

void PgConnection::prepare(const std::string& name, std::string sql) {
    if (conn_ == nullptr) {
        throw PostgresError("PostgreSQL connection is not open");
    }
    const auto found = prepared_.find(name);
    if (found != prepared_.end() && found->second == sql) {
        return;
    }
    PgResult prepared = checked(PQprepare(conn_, name.c_str(), sql.c_str(), 0, nullptr),
                                *this, "prepare");
    (void)prepared;
    prepared_[name] = std::move(sql);
}

PgResult PgConnection::exec_prepared(
    const std::string& name, const std::vector<std::optional<std::string>>& params) const {
    if (conn_ == nullptr) {
        throw PostgresError("PostgreSQL connection is not open");
    }
    if (prepared_.find(name) == prepared_.end()) {
        throw PostgresError("PostgreSQL prepared statement not registered: " + name);
    }
    const auto arrays = to_param_arrays(params);
    return checked(PQexecPrepared(conn_, name.c_str(), static_cast<int>(params.size()),
                                  arrays.values.data(), arrays.lengths.data(),
                                  arrays.formats.data(), 0),
                   *this, "prepared exec");
}

PostgresPool::PostgresPool(PostgresConfig config, std::size_t max_total)
    : config_(std::move(config)), max_total_(max_total) {
    if (max_total_ == 0) {
        throw PostgresError("PostgreSQL pool max_total must be positive");
    }
}

PostgresPool::Borrowed PostgresPool::acquire() {
    std::unique_lock lock{mutex_};
    available_.wait(lock, [this] {
        return closed_ || !idle_.empty() || total_ < max_total_;
    });
    if (closed_) {
        throw PostgresError("PostgreSQL pool is closed");
    }
    if (!idle_.empty()) {
        PgConnection connection = std::move(idle_.front());
        idle_.pop_front();
        if (connection.healthy()) {
            return Borrowed{std::move(connection), this};
        }
        --total_;
        // Fall through and replace the broken connection.
    }
    ++total_;
    try {
        return Borrowed{PgConnection{config_}, this};
    } catch (...) {
        --total_;
        available_.notify_one();
        throw;
    }
}

void PostgresPool::release(PgConnection connection) noexcept {
    std::lock_guard lock{mutex_};
    if (closed_ || !connection.healthy()) {
        if (total_ > 0) {
            --total_;
        }
        return;
    }
    idle_.push_back(std::move(connection));
    available_.notify_one();
}

void PostgresPool::close() noexcept {
    std::lock_guard lock{mutex_};
    closed_ = true;
    idle_.clear();
    available_.notify_all();
}

}  // namespace inerxia::infrastructure::postgres