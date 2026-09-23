#include <gtest/gtest.h>

namespace {
constexpr int kAnswer = 42;
}

TEST(SmokeTest, GoogleTestIsWiredUp) {
    EXPECT_EQ(kAnswer, 42);
}
