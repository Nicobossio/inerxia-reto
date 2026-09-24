#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace inerxia::application {

struct SessionInfo {
    std::string username;
    std::chrono::system_clock::time_point expires_at;
};

// Holds issued admin session tokens. Implementation is storage-neutral (an
// in-memory adapter ships in infrastructure; a persistent store could replace
// it without touching the application layer).
class SessionStore {
public:
    virtual ~SessionStore() = default;

    virtual void save(const std::string& token, const std::string& username,
                      std::chrono::system_clock::time_point expires_at) = 0;
    virtual std::optional<SessionInfo> resolve(const std::string& token) = 0;
    virtual void revoke(const std::string& token) = 0;
};

}  // namespace inerxia::application