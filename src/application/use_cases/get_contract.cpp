#include "application/use_cases/get_contract.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

GetContract::GetContract(ContractRepository& contracts, TimeProvider& clock)
    : contracts_(contracts), clock_(clock) {}

ContractSnapshot GetContract::operator()(const domain::ContractId& contract_id) const {
    const auto contract = contracts_.find_by_id(contract_id);
    if (!contract) {
        throw EntityNotFoundError("Contract not found");
    }
    return ContractSnapshot{*contract, contract->status_as_of(clock_.today())};
}

}  // namespace inerxia::application