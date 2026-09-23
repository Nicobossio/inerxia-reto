#pragma once

#include <chrono>

#include "domain/ids.hpp"
#include "domain/money.hpp"

namespace inerxia::domain {

class Payment {
public:
    Payment(PaymentId id, ContractId contract_id, Money amount,
            std::chrono::year_month_day registered_on);

    [[nodiscard]] const PaymentId& id() const noexcept { return id_; }
    [[nodiscard]] const ContractId& contract_id() const noexcept { return contract_id_; }
    [[nodiscard]] Money amount() const noexcept { return amount_; }
    [[nodiscard]] std::chrono::year_month_day registered_on() const noexcept {
        return registered_on_;
    }

private:
    PaymentId id_;
    ContractId contract_id_;
    Money amount_;
    std::chrono::year_month_day registered_on_;
};

}  // namespace inerxia::domain