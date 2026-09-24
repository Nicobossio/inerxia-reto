#include "application/services/auth_service.hpp"

#include <cctype>
#include <cstddef>
#include <random>

namespace inerxia::application {

AuthService::AuthService(UserRepository& users, PasswordHasher& hasher, SessionStore& sessions,
                         std::chrono::milliseconds session_ttl)
    : users_(users), hasher_(hasher), sessions_(sessions), session_ttl_(session_ttl) {}

std::string AuthService::normalize_username(std::string_view raw) {
    std::size_t first = 0;
    while (first < raw.size() && std::isspace(static_cast<unsigned char>(raw[first]))) {
        ++first;
    }
    std::size_t last = raw.size();
    while (last > first && std::isspace(static_cast<unsigned char>(raw[last - 1]))) {
        --last;
    }
    std::string normalized;
    normalized.reserve(last - first);
    for (std::size_t i = first; i < last; ++i) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(raw[i]))));
    }
    return normalized;
}

std::string AuthService::register_user(const std::string& raw_username,
                                       const std::string& password) {
    const std::string username = normalize_username(raw_username);
    if (username.size() < 3 || username.size() > 32) {
        throw RegistrationError("Username must be between 3 and 32 characters");
    }
    for (const char c : username) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                             c == '_' || c == '.';
        if (!allowed) {
            throw RegistrationError(
                "Username may only contain letters, digits, '-', '_' and '.'");
        }
    }
    if (password.size() < 8) {
        throw RegistrationError("Password must be at least 8 characters long");
    }
    if (users_.find_by_username(username).has_value()) {
        throw UsernameAlreadyRegisteredError("Username already registered: " + username);
    }
    users_.save(User{users_.next_id(), username, hasher_.hash(password), ""});
    return username;
}

std::optional<std::string> AuthService::login(const std::string& raw_username,
                                              const std::string& password) {
    const std::string username = normalize_username(raw_username);
    const auto user = users_.find_by_username(username);
    if (!user || !hasher_.verify(password, user->password_hash)) {
        return std::nullopt;
    }
    const std::string token = issue_token();
    sessions_.save(token, username, std::chrono::system_clock::now() + session_ttl_);
    return token;
}

std::optional<std::string> AuthService::authenticate(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    const auto session = sessions_.resolve(token);
    if (!session) {
        return std::nullopt;
    }
    if (session->expires_at <= std::chrono::system_clock::now()) {
        sessions_.revoke(token);
        return std::nullopt;
    }
    return session->username;
}

void AuthService::logout(const std::string& token) { sessions_.revoke(token); }

std::string AuthService::issue_token() const {
    std::random_device rd;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 32; ++i) {
        const unsigned char value = static_cast<unsigned char>(rd());
        token.push_back(kHex[value >> 4]);
        token.push_back(kHex[value & 0x0F]);
    }
    return token;
}

}  // namespace inerxia::application