#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "application/ports/session_store.hpp"

namespace inerxia::infrastructure::auth {

// Thread-safe in-memory implementation of the SessionStore port. Sessions do
// not survive a restart: acceptable for the operator dashboard, and the adapter
// can be swapped for a persistent one later without touching application code.
class InMemorySessionStore final : public application::SessionStore {
public:
    void save(const std::string& token, const std::string& username,
              std::chrono::system_clock::time_point expires_at) override;
    std::optional<application::SessionInfo> resolve(const std::string& token) override;
    void revoke(const std::string& token) override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, application::SessionInfo> sessions_;
};

}  // namespace inerxia::infrastructure::auth