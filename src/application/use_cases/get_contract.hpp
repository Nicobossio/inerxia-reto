#pragma once

#include "application/ports/contract_repository.hpp"
#include "application/ports/time_provider.hpp"
#include "domain/contract.hpp"
#include "domain/contract_status.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

struct ContractSnapshot {
    domain::Contract contract;
    domain::ContractStatus status;
};

class GetContract {
public:
    GetContract(ContractRepository&, TimeProvider&);

    ContractSnapshot operator()(const domain::ContractId&) const;

private:
    ContractRepository& contracts_;
    TimeProvider& clock_;
};

}  // namespace inerxia::application