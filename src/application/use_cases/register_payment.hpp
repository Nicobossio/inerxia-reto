#pragma once

#include <chrono>
#include <optional>

#include "application/ports/contract_repository.hpp"
#include "application/ports/payment_repository.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/subscriber_repository.hpp"
#include "domain/ids.hpp"
#include "domain/money.hpp"
#include "domain/payment.hpp"

namespace inerxia::application {

struct RegisterPaymentCommand {
    domain::ContractId contract_id;
    domain::Money amount;
    std::chrono::year_month_day paid_on;
};

class RegisterPayment {
public:
    RegisterPayment(ContractRepository&, PaymentRepository&, SubscriberRepository&,
                    RouterGateway&);

    domain::Payment operator()(const RegisterPaymentCommand&) const;

private:
    // Returns the already-registered payment when one with the same content
    // (contract, amount, paid_on) exists on the aggregate.
    [[nodiscard]] std::optional<domain::Payment> find_duplicate(
        const domain::Contract&, const domain::Money&,
        std::chrono::year_month_day paid_on) const;

    ContractRepository& contracts_;
    PaymentRepository& payments_;
    SubscriberRepository& subscribers_;
    RouterGateway& router_;
};

}  // namespace inerxia::application