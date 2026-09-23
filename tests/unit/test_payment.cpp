#include <gtest/gtest.h>

#include <chrono>

#include "domain/payment.hpp"

namespace inerxia::domain::test {
namespace {

using namespace std::chrono;

TEST(PaymentTest, ValidPaymentIsCreated) {
    Payment p{PaymentId{"pay-1"}, ContractId{"ct-1"}, Money::from_cents(15000),
              year{2026}/9/15};

    EXPECT_EQ(p.id().value(), "pay-1");
    EXPECT_EQ(p.contract_id().value(), "ct-1");
    EXPECT_EQ(p.amount(), Money::from_cents(15000));
    EXPECT_EQ(p.registered_on(), year{2026}/9/15);
}

TEST(PaymentTest, MustBeAssociatedWithAContract) {
    EXPECT_THROW(Payment(PaymentId{"pay-1"}, ContractId{""}, Money::from_cents(15000),
                         year{2026}/9/15),
                 DomainError);
}

TEST(PaymentTest, NonPositiveAmountIsRejected) {
    EXPECT_THROW(Payment(PaymentId{"pay-1"}, ContractId{"ct-1"}, Money::from_cents(0),
                         year{2026}/9/15),
                 DomainError);
    EXPECT_THROW(Payment(PaymentId{"pay-1"}, ContractId{"ct-1"}, Money::from_cents(-100),
                         year{2026}/9/15),
                 DomainError);
}

TEST(PaymentTest, InvalidDateIsRejected) {
    EXPECT_THROW(Payment(PaymentId{"pay-1"}, ContractId{"ct-1"}, Money::from_cents(15000),
                         year{0}/month{0}/day{0}),
                 DomainError);
}

}  // namespace
}  // namespace inerxia::domain::test