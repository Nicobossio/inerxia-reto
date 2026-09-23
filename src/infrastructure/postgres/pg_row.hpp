#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "infrastructure/infrastructure_error.hpp"
#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

// Reads one row of a PgResult with null-aware accessors. Out-of-range accesses
// are a programming error and throw PostgresError instead of hitting libpq.
class PgRow {
public:
    PgRow(const PgResult& result, int row) : result_(result), row_(row) {}

    [[nodiscard]] std::optional<std::string> optional_text(int column) const {
        check_range(column);
        if (result_.is_null(row_, column)) {
            return std::nullopt;
        }
        return std::string{result_.text(row_, column)};
    }

    [[nodiscard]] std::string required_text(int column) const {
        const auto value = optional_text(column);
        if (!value.has_value()) {
            throw PostgresError("Unexpected NULL value in the database result (column " +
                                std::to_string(column) + ")");
        }
        return *value;
    }

    [[nodiscard]] std::int64_t bigint(int column) const {
        const std::string value = required_text(column);
        try {
            return std::stoll(value);
        } catch (const std::exception&) {
            throw PostgresError("Invalid BIGINT value in the database result: '" + value +
                                "'");
        }
    }

    [[nodiscard]] int integer(int column) const {
        const std::string value = required_text(column);
        try {
            return std::stoi(value);
        } catch (const std::exception&) {
            throw PostgresError("Invalid INTEGER value in the database result: '" + value +
                                "'");
        }
    }

private:
    void check_range(int column) const {
        if (row_ < 0 || row_ >= result_.row_count() || column < 0 ||
            column >= result_.field_count()) {
            throw PostgresError("Database result access out of range");
        }
    }

    const PgResult& result_;
    int row_;
};

}  // namespace inerxia::infrastructure::postgres