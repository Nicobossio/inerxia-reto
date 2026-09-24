#pragma once

#include <string>

namespace inerxia::application {

// Hashes/verifies operator passwords. The application layer only defines the
// contract; the concrete algorithm (PBKDF2-HMAC-SHA256 via OpenSSL) lives in an
// infrastructure adapter so no cryptographic code reaches the domain.
class PasswordHasher {
public:
    virtual ~PasswordHasher() = default;

    // Returns a self-contained encoded value (algorithm, params, salt, digest).
    virtual std::string hash(const std::string& password) = 0;
    // Verifies a password against an encoded value; false on mismatch or when
    // the encoded value is malformed/unsupported.
    virtual bool verify(const std::string& password, const std::string& encoded) = 0;
};

}  // namespace inerxia::application