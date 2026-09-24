#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "application/application_error.hpp"
#include "application/ports/password_hasher.hpp"
#include "application/ports/session_store.hpp"
#include "application/ports/user_repository.hpp"

namespace inerxia::application {

// Registers platform operators and issues opaque session tokens backed by the
// users table. Pure application service: username normalization and account
// rules live here, password hashing is delegated to the injectable
// PasswordHasher port and sessions to SessionStore. No credentials are ever
// stored in this process; only never-replayed hashes.
class AuthService {
public:
    AuthService(UserRepository& users, PasswordHasher& hasher, SessionStore& sessions,
                std::chrono::milliseconds session_ttl);

    // Creates an operator account. Normalizes the username (trim + lowercase)
    // and returns it. Throws RegistrationError on invalid input and
    // UsernameAlreadyRegisteredError when the username is already taken.
    std::string register_user(const std::string& username, const std::string& password);

    // Returns an opaque session token on valid credentials, otherwise nullopt.
    std::optional<std::string> login(const std::string& username, const std::string& password);
    // Returns the username for a valid (non-expired) token, otherwise nullopt.
    std::optional<std::string> authenticate(const std::string& token);
    void logout(const std::string& token);

private:
    static std::string normalize_username(std::string_view username);
    std::string issue_token() const;

    UserRepository& users_;
    PasswordHasher& hasher_;
    SessionStore& sessions_;
    std::chrono::milliseconds session_ttl_;
};

}  // namespace inerxia::application