#pragma once

#include <chrono>

#include "domain/ids.hpp"
#include "domain/money.hpp"

namespace inerxia::domain {

struct PaymentRegisteredEvent {
    ContractId contract_id;
    Money amount;
    std::chrono::year_month_day date;
};

struct ContractSuspendedEvent {
    ContractId contract_id;
};

struct ContractReactivatedEvent {
    ContractId contract_id;
};

struct SpeedProfileChangedEvent {
    ContractId contract_id;
};

}  // namespace inerxia::domain