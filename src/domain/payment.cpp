#include "domain/payment.hpp"

#include <utility>

#include "domain/domain_error.hpp"

namespace inerxia::domain {

Payment::Payment(PaymentId id, ContractId contract_id, Money amount,
                 std::chrono::year_month_day registered_on)
    : id_(std::move(id)),
      contract_id_(std::move(contract_id)),
      amount_(amount),
      registered_on_(registered_on) {
    if (registered_on_.ok() == false) {
        throw DomainError("Payment date is not a valid calendar date");
    }
    if (!amount_.is_positive()) {
        throw DomainError("Payment amount must be positive");
    }
}

}  // namespace inerxia::domain