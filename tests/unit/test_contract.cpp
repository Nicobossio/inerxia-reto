#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "domain/contract.hpp"

namespace inerxia::domain::test {
namespace {

using namespace std::chrono;

constexpr auto kBillingStart = year{2026}/9/1;
constexpr auto kDueDate = year{2026}/10/1;
constexpr auto kPrice = Money::from_cents(15000);

Contract make_contract(bool valid = true) {
    if (!valid) {
        return Contract{ContractId{""}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                        (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice};
    }
    return Contract{ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                    (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice};
}

TEST(ContractTest, ValidContractStartsActive) {
    const auto c = make_contract();
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Active);
    EXPECT_FALSE(c.is_suspended());
}

TEST(ContractTest, ContractCannotBeActiveWithoutValidInformation) {
    EXPECT_THROW(make_contract(false), DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{""}, PlanId{"plan-1"},
                          (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice),
                 DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{""},
                          (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice),
                 DomainError);
}

TEST(ContractTest, InvalidSpeedIsRejected) {
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                          (SpeedProfile{0, 150}), kBillingStart, kDueDate, kPrice),
                 DomainError);
}

TEST(ContractTest, DueDateMustBeAfterBillingStart) {
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                          (SpeedProfile{300, 150}), kBillingStart, kBillingStart, kPrice),
                 DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                          (SpeedProfile{300, 150}), kDueDate, kBillingStart, kPrice),
                 DomainError);
}

TEST(ContractTest, InvalidDatesAreRejected) {
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                          (SpeedProfile{300, 150}), year{0}/month{0}/day{0}, kDueDate,
                          kPrice),
                 DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                          (SpeedProfile{300, 150}), kBillingStart,
                          year{0}/month{0}/day{0}, kPrice),
                 DomainError);
}

TEST(ContractTest, NonPositivePriceIsRejected) {
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                          (SpeedProfile{300, 150}), kBillingStart, kDueDate,
                          Money::from_cents(0)),
                 DomainError);
}

TEST(ContractTest, NotOverdueOnDueDateItself) {
    const auto c = make_contract();
    EXPECT_EQ(c.status_as_of(kDueDate), ContractStatus::Active);
    EXPECT_FALSE(c.is_overdue(kDueDate));
}

TEST(ContractTest, OverdueAfterDueDateWithoutPayment) {
    const auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};
    EXPECT_TRUE(c.is_overdue(after_due));
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Overdue);
    EXPECT_EQ(c.balance_as_of(after_due), kPrice);
}

TEST(ContractTest, ActiveContractCanBeSuspended) {
    auto c = make_contract();
    c.suspend(SuspensionReason::Manual);
    EXPECT_TRUE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Suspended);
}

TEST(ContractTest, SuspendedContractCannotBeSuspendedAgain) {
    auto c = make_contract();
    c.suspend(SuspensionReason::Manual);
    EXPECT_THROW(c.suspend(SuspensionReason::Manual), DomainError);
}

TEST(ContractTest, SuspendedContractCanBeReactivated) {
    auto c = make_contract();
    c.suspend(SuspensionReason::Manual);
    c.reactivate();
    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Active);
}

TEST(ContractTest, ActiveContractCannotBeReactivated) {
    auto c = make_contract();
    EXPECT_THROW(c.reactivate(), DomainError);
}

TEST(ContractTest, AutoSuspensionWhenOverdue) {
    auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    c.auto_suspend(after_due);
    EXPECT_TRUE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Suspended);
}

TEST(ContractTest, AutoSuspendRejectsNonOverdueContracts) {
    auto c = make_contract();
    EXPECT_THROW(c.auto_suspend(kBillingStart), DomainError);
}

TEST(ContractTest, PaymentMustBeAssociatedWithThisContract) {
    auto c = make_contract();
    Payment foreign{PaymentId{"pay-1"}, ContractId{"otro-ct"},
                    Money::from_cents(15000), year{2026}/9/15};
    EXPECT_THROW(c.attach_payment(foreign), DomainError);
}

TEST(ContractTest, PaymentBeforeDueDatePreventsOverdue) {
    auto c = make_contract();
    Payment p{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, year{2026}/9/15};
    c.attach_payment(p);

    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};
    EXPECT_FALSE(c.is_overdue(after_due));
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Active);
    EXPECT_EQ(c.balance_as_of(after_due), Money::from_cents(0));
}

TEST(ContractTest, PartialPaymentLeavesContractOverdue) {
    auto c = make_contract();
    Payment p{PaymentId{"pay-1"}, ContractId{"ct-1"}, Money::from_cents(5000),
              year{2026}/9/15};
    c.attach_payment(p);

    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};
    EXPECT_TRUE(c.is_overdue(after_due));
    EXPECT_EQ(c.balance_as_of(after_due), Money::from_cents(10000));
}

TEST(ContractTest, FullPaymentAfterDueDateSettlesDebt) {
    auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    c.attach_payment(Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice,
                             after_due});
    EXPECT_FALSE(c.is_overdue(after_due));
    EXPECT_EQ(c.balance_as_of(after_due), Money::from_cents(0));
}

TEST(ContractTest, AutoSuspendedContractIsReactivatedAutomaticallyAfterFullPayment) {
    auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    c.auto_suspend(after_due);
    ASSERT_EQ(c.status_as_of(after_due), ContractStatus::Suspended);

    c.attach_payment(Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice,
                             after_due});

    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Active);
}

TEST(ContractTest, ManuallySuspendedContractIsNotAutoReactivatedByPayment) {
    auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    c.suspend(SuspensionReason::Manual);
    c.attach_payment(Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice,
                             after_due});

    EXPECT_TRUE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Suspended);
}

TEST(ContractTest, ManualReactivateWorksForOverdueSuspension) {
    auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    c.auto_suspend(after_due);
    c.reactivate();
    EXPECT_FALSE(c.is_suspended());
}

TEST(ContractTest, SpeedProfileCanBeChanged) {
    auto c = make_contract();
    c.change_speed_profile((SpeedProfile{600, 300}));
    EXPECT_EQ(c.speed_profile(), (SpeedProfile{600, 300}));
}

TEST(ContractTest, RescheduleUpdatesDueDateKeepingInvariants) {
    auto c = make_contract();

    c.reschedule(year{2026}/12/1);

    EXPECT_EQ(c.due_date(), year{2026}/12/1);
    EXPECT_TRUE(sys_days{c.due_date()} > sys_days{c.billing_start()});
}

TEST(ContractTest, RescheduleRejectsInvalidOrEarlyDueDate) {
    auto c = make_contract();

    EXPECT_THROW(c.reschedule(year{2026}/9/1), DomainError);
    EXPECT_THROW(c.reschedule(year{2026}/8/31), DomainError);
    EXPECT_THROW(c.reschedule(year{0}/month{0}/day{0}), DomainError);
}

TEST(ContractTest, AccessorsReturnExpectedValues) {
    const auto c = make_contract();
    EXPECT_EQ(c.id(), ContractId{"ct-1"});
    EXPECT_EQ(c.subscriber_id(), SubscriberId{"sub-1"});
    EXPECT_EQ(c.plan_id(), PlanId{"plan-1"});
    EXPECT_EQ(c.billing_start(), kBillingStart);
    EXPECT_EQ(c.due_date(), kDueDate);
    EXPECT_EQ(c.price_per_period(), kPrice);
}

TEST(ContractTest, SuspendEmitsDomainEvent) {
    auto c = make_contract();
    c.suspend(SuspensionReason::Manual);

    const auto events = c.take_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<ContractSuspendedEvent>(events.front()));
    EXPECT_EQ(std::get<ContractSuspendedEvent>(events.front()).contract_id,
              ContractId{"ct-1"});
}

TEST(ContractTest, ReactivateEmitsDomainEvent) {
    auto c = make_contract();
    c.suspend(SuspensionReason::Manual);
    auto discarded = c.take_events();
    ASSERT_FALSE(discarded.empty());
    c.reactivate();

    const auto events = c.take_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<ContractReactivatedEvent>(events.front()));
    EXPECT_EQ(std::get<ContractReactivatedEvent>(events.front()).contract_id,
              ContractId{"ct-1"});
}

TEST(ContractTest, PaymentEmitsDomainEvent) {
    auto c = make_contract();
    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, year{2026}/9/15});

    const auto events = c.take_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<PaymentRegisteredEvent>(events.front()));
    EXPECT_EQ(std::get<PaymentRegisteredEvent>(events.front()).contract_id,
              ContractId{"ct-1"});
    EXPECT_EQ(std::get<PaymentRegisteredEvent>(events.front()).amount, kPrice);
}

TEST(ContractTest, AutoReactivateEmitsReactivationEvent) {
    auto c = make_contract();
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    c.auto_suspend(after_due);
    auto events = c.take_events();
    EXPECT_FALSE(events.empty());

    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, after_due});

    events = c.take_events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<PaymentRegisteredEvent>(events[0]));
    EXPECT_TRUE(std::holds_alternative<ContractReactivatedEvent>(events[1]));
}

TEST(ContractTest, RehydrateRestoresPaymentsAndBalance) {
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};
    Payment p{PaymentId{"pay-1"}, ContractId{"ct-1"}, Money::from_cents(10000),
              year{2026}/9/15};

    const auto c =
        Contract::rehydrate(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                            (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice,
                            std::nullopt, std::vector<Payment>{p});

    EXPECT_EQ(c.balance_as_of(after_due), Money::from_cents(5000));
    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Overdue);
}

TEST(ContractTest, RehydrateRestoresSuspensionWithoutEmittingEvents) {
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};

    auto c =
        Contract::rehydrate(ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                            (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice,
                            SuspensionReason::Overdue, std::vector<Payment>{});

    EXPECT_TRUE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Suspended);
    EXPECT_TRUE(c.take_events().empty());
}

TEST(ContractTest, RehydrateStillValidatesCoreInvariants) {
    EXPECT_THROW(Contract::rehydrate(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                                     PlanId{"plan-1"}, (SpeedProfile{300, 150}),
                                     kBillingStart, kBillingStart, kPrice, std::nullopt,
                                     std::vector<Payment>{}),
                 DomainError);
}

TEST(ContractTest, RehydrateRejectsForeignPayment) {
    Payment foreign{PaymentId{"pay-1"}, ContractId{"otro-ct"}, kPrice, year{2026}/9/15};

    EXPECT_THROW(Contract::rehydrate(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                                     PlanId{"plan-1"}, (SpeedProfile{300, 150}),
                                     kBillingStart, kDueDate, kPrice, std::nullopt,
                                     std::vector<Payment>{foreign}),
                 DomainError);
}

TEST(ContractTest, RehydratedContractStillHonoursLifecycle) {
    const auto after_due = year_month_day{sys_days{kDueDate} + days{1}};
    auto c = Contract::rehydrate(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                                 PlanId{"plan-1"}, (SpeedProfile{300, 150}),
                                 kBillingStart, kDueDate, kPrice, std::nullopt,
                                 std::vector<Payment>{});

    c.auto_suspend(after_due);
    EXPECT_TRUE(c.is_suspended());

    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, after_due});
    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(after_due), ContractStatus::Active);
}

TEST(ContractTest, TakenEventsAreCleared) {
    auto c = make_contract();
    c.suspend(SuspensionReason::Manual);
    ASSERT_EQ(c.take_events().size(), 1U);
    EXPECT_TRUE(c.take_events().empty());
}

}  // namespace
}  // namespace inerxia::domain::test