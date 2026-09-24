#include "infrastructure/auth/openssl_pbkdf2_hasher.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace inerxia::infrastructure::auth {

namespace {

std::string to_hex(const unsigned char* data, std::size_t length) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < length; ++i) {
        out << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return out.str();
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::vector<unsigned char> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return {};
    }
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = hex_digit(hex[i]);
        const int low = hex_digit(hex[i + 1]);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return bytes;
}

}  // namespace

std::string OpenSslPbkdf2Hasher::hash(const std::string& password) {
    std::array<unsigned char, kSaltBytes> salt{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw std::runtime_error("OpenSslPbkdf2Hasher: RAND_bytes failed");
    }
    std::array<unsigned char, kKeyBytes> key{};
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), kIterations, EVP_sha256(),
                          static_cast<int>(key.size()), key.data()) != 1) {
        throw std::runtime_error("OpenSslPbkdf2Hasher: PBKDF2 derivation failed");
    }
    return "pbkdf2-sha256$" + std::to_string(kIterations) + "$" + to_hex(salt.data(), salt.size()) +
           "$" + to_hex(key.data(), key.size());
}

bool OpenSslPbkdf2Hasher::verify(const std::string& password, const std::string& encoded) {
    constexpr char kPrefix[] = "pbkdf2-sha256$";
    if (encoded.rfind(kPrefix, 0) != 0) {
        return false;
    }
    const std::string_view with_prefix{encoded};
    const std::string_view body = with_prefix.substr(sizeof(kPrefix) - 1);

    const std::size_t first = body.find('$');
    const std::size_t second = first == std::string_view::npos ? std::string_view::npos
                                                               : body.find('$', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos) {
        return false;
    }
    const std::string_view iterations_str = body.substr(0, first);
    const std::string_view salt_hex = body.substr(first + 1, second - first - 1);
    const std::string_view key_hex = body.substr(second + 1);

    int iterations = 0;
    for (const char c : iterations_str) {
        if (c < '0' || c > '9') {
            return false;
        }
        iterations = iterations * 10 + (c - '0');
        if (iterations > 10'000'000) {
            return false;  // refuse absurd iteration counts (DoS guard)
        }
    }
    if (iterations <= 0) {
        return false;
    }
    const std::vector<unsigned char> salt = from_hex(salt_hex);
    const std::vector<unsigned char> expected = from_hex(key_hex);
    if (salt.size() != kSaltBytes || expected.size() != kKeyBytes) {
        return false;
    }
    std::array<unsigned char, kKeyBytes> derived{};
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          static_cast<int>(derived.size()), derived.data()) != 1) {
        return false;
    }
    return CRYPTO_memcmp(derived.data(), expected.data(), expected.size()) == 0;
}

}  // namespace inerxia::infrastructure::auth