// Unit tests for the OpenSSL-backed PBKDF2-HMAC-SHA256 password hasher.

#include <gtest/gtest.h>

#include <string>

#include "infrastructure/auth/openssl_pbkdf2_hasher.hpp"

namespace {

using inerxia::infrastructure::auth::OpenSslPbkdf2Hasher;

TEST(OpenSslPbkdf2Hasher, HashUsesSelfDescribingEncodedFormat) {
    OpenSslPbkdf2Hasher hasher;
    const std::string encoded = hasher.hash("correct-horse-battery-staple");
    // pbkdf2-sha256$210000$<32 hex salt>$<64 hex key>
    EXPECT_EQ(encoded.rfind("pbkdf2-sha256$210000$", 0), 0u);
    EXPECT_EQ(encoded.size(), 118u);
}

TEST(OpenSslPbkdf2Hasher, RoundTripVerification) {
    OpenSslPbkdf2Hasher hasher;
    const std::string encoded = hasher.hash("s3cret-pass");
    EXPECT_TRUE(hasher.verify("s3cret-pass", encoded));
    EXPECT_FALSE(hasher.verify("wrong-pass", encoded));
    EXPECT_FALSE(hasher.verify("", encoded));
}

TEST(OpenSslPbkdf2Hasher, EqualPasswordsHashDifferently) {
    OpenSslPbkdf2Hasher hasher;
    // A random salt guarantees distinct encoded values for the same password.
    EXPECT_NE(hasher.hash("s3cret-pass"), hasher.hash("s3cret-pass"));
}

TEST(OpenSslPbkdf2Hasher, RejectsMalformedEncodedValues) {
    OpenSslPbkdf2Hasher hasher;
    const std::string encoded = hasher.hash("s3cret-pass");

    EXPECT_FALSE(hasher.verify("s3cret-pass", ""));
    EXPECT_FALSE(hasher.verify("s3cret-pass", "bcrypt$10$abc"));
    EXPECT_FALSE(hasher.verify("s3cret-pass", "pbkdf2-sha256$210000$zzzz"));
    // Correct shape but the digest was tampered with.
    const std::string tampered = encoded.substr(0, encoded.size() - 1) +
                                 (encoded.back() == '0' ? "1" : "0");
    EXPECT_FALSE(hasher.verify("s3cret-pass", tampered));
    // Salt/key must have the expected byte length.
    EXPECT_FALSE(hasher.verify("s3cret-pass", "pbkdf2-sha256$210000$aa$bb"));
    // Absurd iteration counts must be refused without trying to compute.
    EXPECT_FALSE(hasher.verify(
        "s3cret-pass",
        "pbkdf2-sha256$9999999999$aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

}  // namespace