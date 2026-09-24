#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace inerxia::infrastructure {

class InfrastructureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class RouterOSApiError : public InfrastructureError {
public:
    using InfrastructureError::InfrastructureError;

    explicit RouterOSApiError(std::string message, std::optional<int> http_status)
        : InfrastructureError(std::move(message)), http_status_(http_status) {}

    // Present when the failure carries an HTTP response code (e.g. 401, 503).
    // Absent for transport-level failures (connection refused, timeout, DNS).
    [[nodiscard]] const std::optional<int>& http_status() const noexcept {
        return http_status_;
    }

private:
    std::optional<int> http_status_;
};

class PostgresError : public InfrastructureError {
public:
    using InfrastructureError::InfrastructureError;
};

}  // namespace inerxia::infrastructure