#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "domain/subscriber.hpp"

namespace inerxia::domain::test {
namespace {

using namespace std::chrono;

TEST(SubscriberTest, ValidSubscriberIsCreated) {
    Subscriber s{SubscriberId{"sub-1"}, "Ana Lopez", IPAddress{"10.20.30.40"}};

    EXPECT_EQ(s.id().value(), "sub-1");
    EXPECT_EQ(s.name(), "Ana Lopez");
    EXPECT_EQ(s.static_ip(), IPAddress{"10.20.30.40"});
}

TEST(SubscriberTest, EmptyNameIsRejected) {
    EXPECT_THROW(Subscriber(SubscriberId{"sub-1"}, "   ", IPAddress{"10.20.30.40"}),
                 DomainError);
    EXPECT_THROW(Subscriber(SubscriberId{"sub-1"}, "", IPAddress{"10.20.30.40"}),
                 DomainError);
}

TEST(SubscriberTest, InvalidStaticIpIsRejected) {
    EXPECT_THROW(Subscriber(SubscriberId{"sub-1"}, "Ana Lopez", IPAddress{"not-an-ip"}),
                 DomainError);
}

TEST(SubscriberTest, EmptyIdIsRejected) {
    EXPECT_THROW(Subscriber(SubscriberId{""}, "Ana Lopez", IPAddress{"10.20.30.40"}),
                 DomainError);
}

}  // namespace
}  // namespace inerxia::domain::test