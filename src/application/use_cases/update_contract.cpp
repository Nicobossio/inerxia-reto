#include "application/use_cases/update_contract.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

UpdateContract::UpdateContract(ContractRepository& contracts) : contracts_(contracts) {}

domain::Contract UpdateContract::operator()(const UpdateContractCommand& command) const {
    auto contract = contracts_.find_by_id(command.contract_id);
    if (!contract) {
        throw EntityNotFoundError("Contract not found");
    }
    contract->reschedule(command.new_due_date);
    contracts_.save(*contract);
    return *contract;
}

}  // namespace inerxia::application