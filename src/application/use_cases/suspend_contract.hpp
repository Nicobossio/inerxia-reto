#pragma once

#include "application/ports/contract_repository.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/subscriber_repository.hpp"
#include "domain/contract.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

struct SuspendContractCommand {
    domain::ContractId contract_id;
};

class SuspendContract {
public:
    SuspendContract(ContractRepository&, SubscriberRepository&, RouterGateway&);

    domain::Contract operator()(const SuspendContractCommand&) const;

private:
    ContractRepository& contracts_;
    SubscriberRepository& subscribers_;
    RouterGateway& router_;
};

}  // namespace inerxia::application