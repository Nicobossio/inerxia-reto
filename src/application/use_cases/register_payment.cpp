#include "application/use_cases/register_payment.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

RegisterPayment::RegisterPayment(ContractRepository& contracts,
                                 PaymentRepository& payments,
                                 SubscriberRepository& subscribers, RouterGateway& router)
    : contracts_(contracts),
      payments_(payments),
      subscribers_(subscribers),
      router_(router) {}

domain::Payment RegisterPayment::operator()(const RegisterPaymentCommand& command) const {
    auto contract = contracts_.find_by_id(command.contract_id);
    if (!contract) {
        throw EntityNotFoundError("Contract not found");
    }
    const auto subscriber = subscribers_.find_by_id(contract->subscriber_id());
    if (!subscriber) {
        throw EntityNotFoundError("Subscriber not found");
    }

    const bool was_suspended = contract->is_suspended();
    domain::Payment payment{payments_.next_id(), command.contract_id, command.amount,
                            command.paid_on};
    contract->attach_payment(payment);
    contracts_.save(*contract);
    payments_.save(payment);

    if (was_suspended && !contract->is_suspended()) {
        router_.enableUser(contract->id(), subscriber->static_ip());
    }
    return payment;
}

}  // namespace inerxia::application