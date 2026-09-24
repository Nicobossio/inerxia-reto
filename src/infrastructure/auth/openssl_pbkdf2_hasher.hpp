#pragma once

#include <string>

#include "application/ports/password_hasher.hpp"

namespace inerxia::infrastructure::auth {

// PBKDF2-HMAC-SHA256 password hasher backed by OpenSSL libcrypto. Encoded
// values are self-contained and look like:
//   pbkdf2-sha256$<iterations>$<salt-hex>$<derived-key-hex>
// A random 16-byte salt makes equal passwords hash differently; verification
// recomputes the derived key and compares it in constant time (CRYPTO_memcmp).
class OpenSslPbkdf2Hasher final : public application::PasswordHasher {
public:
    std::string hash(const std::string& password) override;
    bool verify(const std::string& password, const std::string& encoded) override;

private:
    static constexpr int kIterations = 210'000;
    static constexpr int kSaltBytes = 16;
    static constexpr int kKeyBytes = 32;
};

}  // namespace inerxia::infrastructure::auth