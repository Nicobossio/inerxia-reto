#pragma once

#include "application/ports/contract_repository.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/subscriber_repository.hpp"
#include "domain/contract.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

struct ReactivateContractCommand {
    domain::ContractId contract_id;
};

class ReactivateContract {
public:
    ReactivateContract(ContractRepository&, SubscriberRepository&, RouterGateway&);

    domain::Contract operator()(const ReactivateContractCommand&) const;

private:
    ContractRepository& contracts_;
    SubscriberRepository& subscribers_;
    RouterGateway& router_;
};

}  // namespace inerxia::application