#pragma once

#include <stdexcept>

namespace inerxia::application {

class EntityNotFoundError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Raised when registration input violates an operator-account rule (invalid
// username charset/length, weak password). Maps to HTTP 422.
class RegistrationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Raised when a registration targets a username that is already taken on the
// platform. Maps to HTTP 409.
class UsernameAlreadyRegisteredError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace inerxia::application