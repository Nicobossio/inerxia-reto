#pragma once

#include <stdexcept>
#include <string>

namespace inerxia::infrastructure {

class InfrastructureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class RouterOSApiError : public InfrastructureError {
public:
    using InfrastructureError::InfrastructureError;
};

}  // namespace inerxia::infrastructure