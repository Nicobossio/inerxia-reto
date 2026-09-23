#include <gtest/gtest.h>

#include "domain/ids.hpp"
#include "domain/ip_address.hpp"
#include "domain/money.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::domain::test {
namespace {

TEST(IdTest, EmptyIdIsRejected) {
    EXPECT_THROW(SubscriberId{""}, DomainError);
    EXPECT_THROW(PlanId{""}, DomainError);
    EXPECT_THROW(ContractId{""}, DomainError);
    EXPECT_THROW(PaymentId{""}, DomainError);
}

TEST(IdTest, EqualityAndValue) {
    EXPECT_EQ(ContractId{"a"}, ContractId{"a"});
    EXPECT_NE(ContractId{"a"}, ContractId{"b"});
    EXPECT_EQ(ContractId{"a"}.value(), "a");

    static_assert(!std::is_same_v<ContractId, PlanId>);
}

TEST(MoneyTest, EqualityAndArithmetic) {
    EXPECT_EQ(Money::from_cents(15000), Money::from_cents(15000));
    EXPECT_NE(Money::from_cents(15000), Money::from_cents(100));

    EXPECT_EQ(Money::from_cents(15000) + Money::from_cents(500),
              Money::from_cents(15500));
    EXPECT_EQ(Money::from_cents(15000) - Money::from_cents(100),
              Money::from_cents(14900));

    EXPECT_TRUE(Money::from_cents(1).is_positive());
    EXPECT_FALSE(Money::from_cents(0).is_positive());
    EXPECT_TRUE(Money::from_cents(0).is_non_negative());
    EXPECT_FALSE(Money::from_cents(-1).is_non_negative());
}

TEST(SpeedProfileTest, ValidSpeedIsAccepted) {
    EXPECT_EQ((SpeedProfile{300, 150}).download_mbps(), 300);
    EXPECT_EQ((SpeedProfile{300, 150}).upload_mbps(), 150);
}

TEST(SpeedProfileTest, NonPositiveSpeedIsRejected) {
    EXPECT_THROW((SpeedProfile{0, 150}), DomainError);
    EXPECT_THROW((SpeedProfile{300, -1}), DomainError);
    EXPECT_THROW((SpeedProfile{0, 0}), DomainError);
}

TEST(SpeedProfileTest, Equality) {
    EXPECT_EQ((SpeedProfile{300, 150}), (SpeedProfile{300, 150}));
    EXPECT_NE((SpeedProfile{300, 150}), (SpeedProfile{600, 300}));
}

TEST(IPAddressTest, ValidIpv4IsAccepted) {
    EXPECT_EQ(IPAddress{"10.20.30.40"}, IPAddress{"10.20.30.40"});
    EXPECT_EQ(IPAddress{"192.168.1.1"}, IPAddress{"192.168.1.1"});
    EXPECT_EQ(IPAddress{"0.0.0.0"}, IPAddress{"0.0.0.0"});
    EXPECT_EQ(IPAddress{"255.255.255.255"}, IPAddress{"255.255.255.255"});
}

TEST(IPAddressTest, InvalidIpv4IsRejected) {
    EXPECT_THROW(IPAddress{""}, DomainError);
    EXPECT_THROW(IPAddress{"10.20.30"}, DomainError);
    EXPECT_THROW(IPAddress{"10.20.30.40.50"}, DomainError);
    EXPECT_THROW(IPAddress{"256.20.30.40"}, DomainError);
    EXPECT_THROW(IPAddress{"10.20.30.-1"}, DomainError);
    EXPECT_THROW(IPAddress{"aa.bb.cc.dd"}, DomainError);
    EXPECT_THROW(IPAddress{"10.20.30.4a"}, DomainError);
    EXPECT_THROW(IPAddress{"10 .20.30.40"}, DomainError);
}

}  // namespace
}  // namespace inerxia::domain::test