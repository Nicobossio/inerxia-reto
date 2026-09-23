#include <gtest/gtest.h>

#include "domain/internet_plan.hpp"

namespace inerxia::domain::test {
namespace {

TEST(InternetPlanTest, ValidPlanIsCreated) {
    InternetPlan p{PlanId{"plan-1"}, "Fibra 300", (SpeedProfile{300, 150}),
                   Money::from_cents(15000)};

    EXPECT_EQ(p.id().value(), "plan-1");
    EXPECT_EQ(p.name(), "Fibra 300");
    EXPECT_EQ(p.speed(), (SpeedProfile{300, 150}));
    EXPECT_EQ(p.monthly_price(), Money::from_cents(15000));
}

TEST(InternetPlanTest, EmptyNameIsRejected) {
    EXPECT_THROW(InternetPlan(PlanId{"plan-1"}, "", (SpeedProfile{300, 150}),
                              Money::from_cents(15000)),
                 DomainError);
}

TEST(InternetPlanTest, NonPositivePriceIsRejected) {
    EXPECT_THROW(InternetPlan(PlanId{"plan-1"}, "Fibra 300", (SpeedProfile{300, 150}),
                              Money::from_cents(0)),
                 DomainError);
    EXPECT_THROW(InternetPlan(PlanId{"plan-1"}, "Fibra 300", (SpeedProfile{300, 150}),
                              Money::from_cents(-150)),
                 DomainError);
}

TEST(InternetPlanTest, InvalidSpeedIsRejected) {
    EXPECT_THROW(InternetPlan(PlanId{"plan-1"}, "Fibra 300", (SpeedProfile{0, 150}),
                              Money::from_cents(15000)),
                 DomainError);
}

}  // namespace
}  // namespace inerxia::domain::test