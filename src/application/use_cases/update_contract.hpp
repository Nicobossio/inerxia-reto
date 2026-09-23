#pragma once

#include <chrono>

#include "application/ports/contract_repository.hpp"
#include "domain/contract.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

struct UpdateContractCommand {
    domain::ContractId contract_id;
    std::chrono::year_month_day new_due_date;
};

class UpdateContract {
public:
    explicit UpdateContract(ContractRepository&);

    domain::Contract operator()(const UpdateContractCommand&) const;

private:
    ContractRepository& contracts_;
};

}  // namespace inerxia::application