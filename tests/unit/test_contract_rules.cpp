#include <gtest/gtest.h>

#include <chrono>

#include "domain/contract.hpp"

namespace inerxia::domain::test {
namespace {

using namespace std::chrono;

constexpr auto kBillingStart = year{2026}/9/1;
constexpr auto kDueDate = year{2026}/10/1;
constexpr auto kPrice = Money::from_cents(15000);

Contract make_contract() {
    return Contract{ContractId{"ct-1"}, SubscriberId{"sub-1"}, PlanId{"plan-1"},
                    (SpeedProfile{300, 150}), kBillingStart, kDueDate, kPrice};
}

year_month_day after_due() { return year_month_day{sys_days{kDueDate} + days{1}}; }

// Rule 1: crear un contrato válido.
TEST(CriticalRulesTest, CreatingValidContractProducesSchedulableAggregate) {
    auto c = make_contract();

    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Active);
    EXPECT_FALSE(c.is_overdue(kBillingStart));

    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, kBillingStart});
    EXPECT_EQ(c.balance_as_of(after_due()), Money::from_cents(0));
    EXPECT_EQ(c.status_as_of(after_due()), ContractStatus::Active);
}

// Rule 2: rechazar un contrato con fecha inválida.
TEST(CriticalRulesTest, ContractWithInvalidDatesIsRejected) {
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                          PlanId{"plan-1"}, (SpeedProfile{300, 150}),
                          year{0}/month{0}/day{0}, kDueDate, kPrice),
                 DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                          PlanId{"plan-1"}, (SpeedProfile{300, 150}), kBillingStart,
                          year{0}/month{0}/day{0}, kPrice),
                 DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                          PlanId{"plan-1"}, (SpeedProfile{300, 150}), kBillingStart,
                          kBillingStart, kPrice),
                 DomainError);
    EXPECT_THROW(Contract(ContractId{"ct-1"}, SubscriberId{"sub-1"},
                          PlanId{"plan-1"}, (SpeedProfile{300, 150}), kDueDate,
                          kBillingStart, kPrice),
                 DomainError);
}

// Rule 3: registrar un pago.
TEST(CriticalRulesTest, RegisteringPaymentSettlesDebtAndKeepsContractCurrent) {
    auto c = make_contract();

    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, after_due()});

    EXPECT_EQ(c.balance_as_of(after_due()), Money::from_cents(0));
    EXPECT_FALSE(c.is_overdue(after_due()));
    EXPECT_EQ(c.status_as_of(after_due()), ContractStatus::Active);
}

TEST(CriticalRulesTest, PaymentForAnotherContractIsRejected) {
    auto c = make_contract();
    Payment foreign{PaymentId{"pay-1"}, ContractId{"otro"}, kPrice, after_due()};

    EXPECT_THROW(c.attach_payment(foreign), DomainError);
}

// Rule 4: detectar contrato vencido sin pago.
TEST(CriticalRulesTest, ContractIsOverdueOnceDueDatePassedWithoutPayment) {
    auto c = make_contract();

    EXPECT_FALSE(c.is_overdue(kDueDate));
    EXPECT_EQ(c.status_as_of(kDueDate), ContractStatus::Active);

    EXPECT_TRUE(c.is_overdue(after_due()));
    EXPECT_EQ(c.status_as_of(after_due()), ContractStatus::Overdue);
}

// Rule 5: suspender contrato.
TEST(CriticalRulesTest, SuspendTransitionsContractToSuspended) {
    auto c = make_contract();

    c.suspend(SuspensionReason::Manual);

    EXPECT_TRUE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Suspended);
}

// Rule 6: reactivar contrato.
TEST(CriticalRulesTest, ReactivateTransitionsSuspendedContractBackToActive) {
    auto c = make_contract();

    c.suspend(SuspensionReason::Manual);
    c.reactivate();

    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Active);
}

// Rule 7: evitar suspender un contrato que está pagado.
TEST(CriticalRulesTest, AutoSuspendRejectsContractThatAlreadyPaid) {
    auto c = make_contract();
    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, after_due()});

    EXPECT_THROW(c.auto_suspend(after_due()), DomainError);
    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.status_as_of(after_due()), ContractStatus::Active);
}

// Design decision: a paid contract may still be suspended by an explicit
// operator action; only the automatic (overdue-driven) path is forbidden below.
TEST(CriticalRulesTest, ManualSuspendOfPaidContractRemainsAnOperatorAction) {
    auto c = make_contract();
    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, kBillingStart});

    EXPECT_NO_THROW(c.suspend(SuspensionReason::Manual));
    EXPECT_TRUE(c.is_suspended());
}

// Rules 8 & 9: cambiar el perfil de velocidad / las megas contratadas.
TEST(CriticalRulesTest, ChangingSpeedProfileUpdatesContractedMegasCoherently) {
    auto c = make_contract();
    ASSERT_EQ(c.speed_profile(), (SpeedProfile{300, 150}));

    c.change_speed_profile(((SpeedProfile{600, 300})));

    EXPECT_EQ(c.speed_profile(), (SpeedProfile{600, 300}));
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Active);
    EXPECT_FALSE(c.is_overdue(kBillingStart));

    c.suspend(SuspensionReason::Manual);
    c.reactivate();
    EXPECT_EQ(c.status_as_of(kBillingStart), ContractStatus::Active);

    c.change_speed_profile(((SpeedProfile{1000, 500})));
    EXPECT_EQ(c.speed_profile(), (SpeedProfile{1000, 500}));
}

TEST(CriticalRulesTest, SpeedProfileChangeEmitsDomainEvent) {
    auto c = make_contract();

    c.change_speed_profile(((SpeedProfile{600, 300})));

    const auto events = c.take_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<SpeedProfileChangedEvent>(events.front()));
    EXPECT_EQ(std::get<SpeedProfileChangedEvent>(events.front()).contract_id,
              ContractId{"ct-1"});
}

TEST(CriticalRulesTest, NoSpeedProfileEventWhenMegasUnchanged) {
    auto c = make_contract();

    c.change_speed_profile(((SpeedProfile{300, 150})));

    EXPECT_TRUE(c.take_events().empty());
}

// Rule 10: mantener las invariantes del dominio a lo largo del ciclo de vida.
TEST(CriticalRulesTest, DomainInvariantsHoldAcrossFullLifecycle) {
    auto c = make_contract();

    c.auto_suspend(after_due());
    ASSERT_TRUE(c.is_suspended());

    c.attach_payment(
        Payment{PaymentId{"pay-1"}, ContractId{"ct-1"}, kPrice, after_due()});
    EXPECT_FALSE(c.is_suspended());
    EXPECT_EQ(c.balance_as_of(after_due()), Money::from_cents(0));
    EXPECT_EQ(c.status_as_of(after_due()), ContractStatus::Active);

    c.suspend(SuspensionReason::Manual);
    EXPECT_THROW(c.suspend(SuspensionReason::Manual), DomainError);
    c.reactivate();
    EXPECT_EQ(c.status_as_of(after_due()), ContractStatus::Active);

    c.change_speed_profile(((SpeedProfile{1000, 500})));
    EXPECT_EQ(c.speed_profile(), (SpeedProfile{1000, 500}));

    EXPECT_EQ(c.due_date(), kDueDate);
    EXPECT_TRUE(sys_days{c.due_date()} > sys_days{c.billing_start()});
    EXPECT_TRUE(c.price_per_period().is_positive());
    EXPECT_EQ(c.id(), ContractId{"ct-1"});
}

}  // namespace
}  // namespace inerxia::domain::test