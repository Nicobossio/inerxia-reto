#pragma once

#include <optional>
#include <string>

namespace inerxia::application {

// An operator account on the platform, backed by the users table. Operator
// accounts are platform-level (not ISP billing entities), so like AuditEntry
// they live at the application boundary as a plain value.
struct User {
    std::string id;             // storage-assigned (UUID v4)
    std::string username;       // normalized: trimmed + lowercase
    std::string password_hash;  // PBKDF2-HMAC-SHA256 encoded value
    std::string created_at;     // ISO-8601 UTC instant (storage-provided)
};

// Store for operator accounts. Implementations guarantee the username unique
// constraint: a conflicting save throws UsernameAlreadyRegisteredError.
class UserRepository {
public:
    virtual ~UserRepository() = default;

    virtual std::string next_id() = 0;
    virtual std::optional<User> find_by_username(const std::string& username) const = 0;
    virtual void save(const User& user) = 0;
};

}  // namespace inerxia::application