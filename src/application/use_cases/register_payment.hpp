#pragma once

#include <chrono>

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
    ContractRepository& contracts_;
    PaymentRepository& payments_;
    SubscriberRepository& subscribers_;
    RouterGateway& router_;
};

}  // namespace inerxia::application