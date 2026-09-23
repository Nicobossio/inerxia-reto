#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "application/application_error.hpp"
#include "domain/contract_status.hpp"
#include "domain/domain_error.hpp"
#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::api {

// Strict ISO-8601 date parsing (YYYY-MM-DD). Returns nullopt on any malformed
// or non-existent calendar date so handlers can answer 400 instead of 422.
inline std::optional<std::chrono::year_month_day> parse_iso_date(std::string_view value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return std::nullopt;
    }
    const auto digits = [value](std::size_t start, std::size_t count) -> int {
        int result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const char c = value[start + i];
            if (c < '0' || c > '9') {
                return -1;
            }
            result = result * 10 + (c - '0');
        }
        return result;
    };
    const int year = digits(0, 4);
    const int month = digits(5, 2);
    const int day = digits(8, 2);
    if (year < 1 || month < 1 || day < 1) {
        return std::nullopt;
    }
    const auto date = std::chrono::year{year} / std::chrono::month{
                                                     static_cast<unsigned>(month)} /
                      std::chrono::day{static_cast<unsigned>(day)};
    if (!date.ok()) {
        return std::nullopt;
    }
    return date;
}

inline std::string format_iso_date(std::chrono::year_month_day date) {
    const auto two_digits = [](unsigned value) {
        return value < 10 ? std::string{"0"} + std::to_string(value)
                          : std::to_string(value);
    };
    return std::to_string(static_cast<int>(date.year())) + "-" +
           two_digits(static_cast<unsigned>(date.month())) + "-" +
           two_digits(static_cast<unsigned>(date.day()));
}

inline std::string_view status_to_string(domain::ContractStatus status) {
    switch (status) {
        case domain::ContractStatus::Active:
            return "active";
        case domain::ContractStatus::Overdue:
            return "overdue";
        case domain::ContractStatus::Suspended:
            return "suspended";
    }
    return "unknown";
}

// Error raised by handlers for HTTP-level problems that are neither domain nor
// infrastructure failures (malformed input, unknown ids after parsing, etc.).
struct HttpRequestError {
    int status = 400;
    std::string_view code = "bad_request";
    std::string message;
};

// Parses a required ISO-8601 date field; throws HttpRequestError on malformed
// or non-existent dates (mapped to 400), and nlohmann exceptions for missing
// fields/type mismatches (also 400).
inline std::chrono::year_month_day required_iso_date(const nlohmann::json& body,
                                                     std::string_view key) {
    const auto value = body.at(key).get<std::string>();
    const auto date = parse_iso_date(value);
    if (!date) {
        throw HttpRequestError{400, "bad_request",
                               "Invalid ISO-8601 date for '" + std::string{key} +
                                   "': " + value};
    }
    return *date;
}

inline nlohmann::json parse_body(const httplib::Request& request) {
    if (request.body.empty()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(request.body, /* parser_callback_t */ nullptr,
                                 /* allow_exceptions */ true);
}

inline void reply_error(httplib::Response& response, int status, std::string_view code,
                        const std::string& message) {
    response.status = status;
    response.set_content(
        nlohmann::json{{"error", code}, {"message", message}}.dump(), "application/json");
}

// Runs a request handler and translates domain/application/infrastructure
// exceptions into the appropriate HTTP error contract. Controllers themselves
// contain no business logic; this is pure HTTP-level mapping.
template <typename F>
void run_and_handle(F&& handler, httplib::Response& response) {
    try {
        handler();
    } catch (const HttpRequestError& error) {
        reply_error(response, error.status, error.code, error.message);
    } catch (const application::EntityNotFoundError& error) {
        reply_error(response, 404, "not_found", error.what());
    } catch (const domain::DomainError& error) {
        reply_error(response, 422, "domain_rule", error.what());
    } catch (const infrastructure::InfrastructureError& error) {
        reply_error(response, 503, "infrastructure", error.what());
    } catch (const nlohmann::json::exception& error) {
        reply_error(response, 400, "bad_request", error.what());
    } catch (const std::exception& error) {
        reply_error(response, 500, "internal", error.what());
    }
}

}  // namespace inerxia::api