#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::infrastructure::postgres {

// Formats a calendar date as the PostgreSQL 'date' literal YYYY-MM-DD.
inline std::string to_sql_date(std::chrono::year_month_day date) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-'
        << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
        << std::setw(2) << static_cast<unsigned>(date.day());
    return out.str();
}

// Parses a PostgreSQL 'date' text literal back into a calendar date.
inline std::chrono::year_month_day from_sql_date(std::string_view value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        throw PostgresError("Invalid PostgreSQL date literal: '" + std::string{value} + "'");
    }
    auto parse = [](std::string_view digits) -> int {
        int result = 0;
        for (const char c : digits) {
            if (c < '0' || c > '9') {
                throw PostgresError("Invalid PostgreSQL date literal");
            }
            result = result * 10 + (c - '0');
        }
        return result;
    };
    const std::chrono::year_month_day date{
        std::chrono::year{parse(value.substr(0, 4))},
        std::chrono::month{static_cast<unsigned>(parse(value.substr(5, 2)))},
        std::chrono::day{static_cast<unsigned>(parse(value.substr(8, 2)))}};
    if (!date.ok()) {
        throw PostgresError("Invalid PostgreSQL date literal: '" + std::string{value} + "'");
    }
    return date;
}

// RFC 4122 version 4 UUID used by the repositories as the aggregate id source.
inline std::string next_uuid_v4() {
    static std::mt19937_64 engine = [] {
        std::random_device device;
        const std::mt19937_64::result_type seed =
            (static_cast<std::mt19937_64::result_type>(device()) << 32) ^
            static_cast<std::mt19937_64::result_type>(device());
        return std::mt19937_64{seed};
    }();

    std::uint32_t words[4];
    for (auto& word : words) {
        word = static_cast<std::uint32_t>(engine());
    }
    std::uint8_t bytes[16];
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[i * 4 + 0] = static_cast<std::uint8_t>(words[i] >> 24);
        bytes[i * 4 + 1] = static_cast<std::uint8_t>(words[i] >> 16);
        bytes[i * 4 + 2] = static_cast<std::uint8_t>(words[i] >> 8);
        bytes[i * 4 + 3] = static_cast<std::uint8_t>(words[i]);
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);  // version 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);  // variant 10xx

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < 16; ++i) {
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            out << '-';
        }
    }
    return out.str();
}

}  // namespace inerxia::infrastructure::postgres