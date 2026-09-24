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

    // Idempotency: registering the same payment again (retry of a request that
    // previously failed mid-flight, or an operator double-entry) must never
    // credit the contract a second time.
    if (const auto existing = find_duplicate(*contract, command.amount, command.paid_on)) {
        // Reconciliation with RouterOS: when a previous attempt failed at the
        // enableUser() step, the contract was already persisted as active in
        // PostgreSQL while MikroTik still has the user disabled. Re-asserting
        // the enabled state here is idempotent on the router and converges the
        // two sides. A still-suspended contract means the debt was not cleared
        // (e.g. partial payment), so nothing to re-assert.
        if (!contract->is_suspended()) {
            router_.enableUser(contract->id(), subscriber->static_ip());
        }
        return *existing;
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

std::optional<domain::Payment> RegisterPayment::find_duplicate(
    const domain::Contract& contract, const domain::Money& amount,
    std::chrono::year_month_day paid_on) const {
    for (const auto& payment : contract.payments()) {
        if (payment.amount() == amount && payment.registered_on() == paid_on) {
            return payment;
        }
    }
    return std::nullopt;
}

}  // namespace inerxia::application