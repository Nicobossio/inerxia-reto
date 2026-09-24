#include "application/use_cases/list_contracts.hpp"

namespace inerxia::application {

ListContracts::ListContracts(ContractRepository& contracts, TimeProvider& clock)
    : contracts_{contracts}, clock_{clock} {}

std::vector<ContractSnapshot> ListContracts::operator()() const {
    std::vector<ContractSnapshot> snapshots;
    snapshots.reserve(contracts_.all().size());
    for (const auto& contract : contracts_.all()) {
        snapshots.push_back(ContractSnapshot{contract, contract.status_as_of(clock_.today())});
    }
    return snapshots;
}

}  // namespace inerxia::application