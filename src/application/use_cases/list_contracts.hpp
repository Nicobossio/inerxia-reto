#pragma once

#include <vector>

#include "application/ports/contract_repository.hpp"
#include "application/ports/time_provider.hpp"
#include "application/use_cases/get_contract.hpp"

namespace inerxia::application {

// Read-model listing of every contract with its status computed from the
// TimeProvider, so the operator dashboard can show who is current and who is
// overdue/suspended without duplicating the domain status rule.
class ListContracts {
public:
    ListContracts(ContractRepository&, TimeProvider&);

    std::vector<ContractSnapshot> operator()() const;

private:
    ContractRepository& contracts_;
    TimeProvider& clock_;
};

}  // namespace inerxia::application