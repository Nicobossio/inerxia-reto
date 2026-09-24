// Unit tests for the operator authentication/registration service.

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>

#include "application/services/auth_service.hpp"
#include "infrastructure/auth/in_memory_session_store.hpp"
#include "application_fakes.hpp"

namespace {

using namespace std::chrono;
using inerxia::application::AuthService;
using inerxia::application::RegistrationError;
using inerxia::application::UsernameAlreadyRegisteredError;
using inerxia::application::test::InMemoryUserRepository;
using inerxia::application::test::PlainPasswordHasher;
using inerxia::infrastructure::auth::InMemorySessionStore;

class AuthServiceTest : public ::testing::Test {
protected:
    AuthService make_auth(milliseconds ttl = hours{12}) {
        return AuthService{users_, hasher_, sessions_, ttl};
    }

    InMemoryUserRepository users_;
    PlainPasswordHasher hasher_;
    InMemorySessionStore sessions_;
};

TEST_F(AuthServiceTest, RegisterCreatesUserWithNormalizedUsername) {
    AuthService auth = make_auth();
    const std::string registered = auth.register_user("  Operator.One ", "s3cret-pass");
    EXPECT_EQ(registered, "operator.one");
    const auto user = users_.find_by_username("operator.one");
    ASSERT_TRUE(user.has_value());
    EXPECT_FALSE(user->password_hash.empty());
    EXPECT_FALSE(user->id.empty());
}

TEST_F(AuthServiceTest, RegisterRejectsDuplicateUsername) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    EXPECT_THROW(auth.register_user("  Operator ", "other-pass"), UsernameAlreadyRegisteredError);
    EXPECT_EQ(users_.size(), 1u);
}

TEST_F(AuthServiceTest, RegisterRejectsShortPasswords) {
    AuthService auth = make_auth();
    EXPECT_THROW(auth.register_user("operator", "1234567"), RegistrationError);
    EXPECT_THROW(auth.register_user("operator", ""), RegistrationError);
    EXPECT_EQ(users_.size(), 0u);
}

TEST_F(AuthServiceTest, RegisterRejectsInvalidUsernames) {
    AuthService auth = make_auth();
    EXPECT_THROW(auth.register_user("ab", "s3cret-pass"), RegistrationError);       // too short
    EXPECT_THROW(auth.register_user(std::string(33, 'a'), "s3cret-pass"),            // too long
                 RegistrationError);
    EXPECT_THROW(auth.register_user("has space", "s3cret-pass"), RegistrationError);
    EXPECT_THROW(auth.register_user("user@name", "s3cret-pass"), RegistrationError);
    EXPECT_THROW(auth.register_user(" Ünïcode ", "s3cret-pass"), RegistrationError);
    EXPECT_EQ(users_.size(), 0u);
}

TEST_F(AuthServiceTest, LoginWithValidCredentialsReturnsAnOpaqueToken) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    const auto token = auth.login("operator", "s3cret-pass");
    ASSERT_TRUE(token.has_value());
    ASSERT_EQ(token->size(), 64u);
    EXPECT_TRUE(auth.authenticate(*token).has_value());
}

TEST_F(AuthServiceTest, AuthenticateResolvesTheLoggedInUsername) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    const auto token = auth.login("operator", "s3cret-pass");
    ASSERT_TRUE(token.has_value());
    const auto username = auth.authenticate(*token);
    ASSERT_TRUE(username.has_value());
    EXPECT_EQ(*username, "operator");
}

TEST_F(AuthServiceTest, LoginNormalizesTheUsername) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    const auto token = auth.login("  OPERATOR ", "s3cret-pass");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*auth.authenticate(*token), "operator");
}

TEST_F(AuthServiceTest, LoginRejectsWrongPassword) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    EXPECT_FALSE(auth.login("operator", "wrong").has_value());
}

TEST_F(AuthServiceTest, LoginRejectsUnregisteredUsername) {
    AuthService auth = make_auth();
    EXPECT_FALSE(auth.login("ghost", "s3cret-pass").has_value());
    EXPECT_FALSE(auth.login("", "").has_value());
}

TEST_F(AuthServiceTest, TwoLoginsIssueDistinctTokens) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    const auto first = auth.login("operator", "s3cret-pass");
    const auto second = auth.login("operator", "s3cret-pass");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    // Both remain valid simultaneously.
    EXPECT_TRUE(auth.authenticate(*first).has_value());
    EXPECT_TRUE(auth.authenticate(*second).has_value());
}

TEST_F(AuthServiceTest, AuthenticateRejectsUnknownOrEmptyTokens) {
    AuthService auth = make_auth();
    EXPECT_FALSE(auth.authenticate("").has_value());
    EXPECT_FALSE(auth.authenticate("bogus-token").has_value());
}

TEST_F(AuthServiceTest, LogoutRevokesTheToken) {
    AuthService auth = make_auth();
    auth.register_user("operator", "s3cret-pass");
    const auto token = auth.login("operator", "s3cret-pass");
    ASSERT_TRUE(token.has_value());
    auth.logout(*token);
    EXPECT_FALSE(auth.authenticate(*token).has_value());
}

TEST_F(AuthServiceTest, ExpiredSessionIsRejectedAndPurged) {
    // Zero TTL makes the session expire immediately.
    AuthService auth = make_auth(milliseconds{0});
    auth.register_user("operator", "s3cret-pass");
    const auto token = auth.login("operator", "s3cret-pass");
    ASSERT_TRUE(token.has_value());
    EXPECT_FALSE(auth.authenticate(*token).has_value());
}

TEST_F(AuthServiceTest, RegistrationWithoutCredentials) {
    AuthService auth = make_auth();
    const auto token = auth.login("operator", "s3cret-pass");
    EXPECT_FALSE(token.has_value());
}

}  // namespace