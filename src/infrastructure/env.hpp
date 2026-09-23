#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "infrastructure/infrastructure_error.hpp"

namespace inerxia::infrastructure {

inline std::string env_value_or(const char* name, std::string fallback) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string{value};
    }
    return fallback;
}

inline std::string env_value_required(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string{value};
    }
    throw InfrastructureError(std::string("Missing required environment variable: ") + name);
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