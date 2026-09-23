#pragma once

#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::infrastructure {

inline bool is_blank(const std::string& value) {
    for (const char c : value) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return false;
        }
    }
    return true;
}

inline std::string env_value_or(const char* name, std::string fallback) {
    if (const char* value = std::getenv(name); value != nullptr && !is_blank(value)) {
        return std::string{value};
    }
    return fallback;
}

inline std::string env_value_required(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && !is_blank(value)) {
        return std::string{value};
    }
    throw InfrastructureError(std::string("Missing required environment variable: ") + name);
}

// Reports every missing variable at once so operators see the full picture.
inline void require_environment_variables(std::initializer_list<const char*> names) {
    std::string missing;
    for (const char* name : names) {
        const char* value = std::getenv(name);
        if (value == nullptr || is_blank(value)) {
            if (!missing.empty()) {
                missing += ", ";
            }
            missing += name;
        }
    }
    if (!missing.empty()) {
        throw InfrastructureError("Missing required environment variable(s): " + missing);
    }
}

inline bool env_bool_or(const char* name, bool fallback) {
    const std::string value = env_value_or(name, fallback ? "true" : "false");
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" ||
           value == "on";
}

inline int env_positive_int_or(const char* name, int fallback) {
    const std::string value = env_value_or(name, std::to_string(fallback));
    try {
        const std::size_t pos = value.find_first_not_of("0123456789");
        if (pos != std::string::npos) {
            throw std::invalid_argument("not an integer");
        }
        const int parsed = std::stoi(value);
        if (parsed <= 0) {
            throw std::invalid_argument("not positive");
        }
        return parsed;
    } catch (const std::exception&) {
        throw InfrastructureError("Invalid value for environment variable " +
                                  std::string(name) + ": " + value +
                                  " (expected a positive integer)");
    }
}

}  // namespace inerxia::infrastructure