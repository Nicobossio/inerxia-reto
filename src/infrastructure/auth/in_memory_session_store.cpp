#include "infrastructure/auth/in_memory_session_store.hpp"

namespace inerxia::infrastructure::auth {

void InMemorySessionStore::save(const std::string& token, const std::string& username,
                                std::chrono::system_clock::time_point expires_at) {
    std::lock_guard lock{mutex_};
    sessions_.insert_or_assign(token, application::SessionInfo{username, expires_at});
}

std::optional<application::SessionInfo> InMemorySessionStore::resolve(const std::string& token) {
    std::lock_guard lock{mutex_};
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    if (it->second.expires_at <= std::chrono::system_clock::now()) {
        sessions_.erase(it);
        return std::nullopt;
    }
    return it->second;
}

void InMemorySessionStore::revoke(const std::string& token) {
    std::lock_guard lock{mutex_};
    sessions_.erase(token);
}

}  // namespace inerxia::infrastructure::auth