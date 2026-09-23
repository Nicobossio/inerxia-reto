#pragma once

#include <vector>

#include "application/ports/contract_repository.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/subscriber_repository.hpp"
#include "application/ports/time_provider.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

class EvaluateExpiredContracts {
public:
    EvaluateExpiredContracts(ContractRepository&, SubscriberRepository&, RouterGateway&,
                             TimeProvider&);

    std::vector<domain::ContractId> operator()() const;

private:
    ContractRepository& contracts_;
    SubscriberRepository& subscribers_;
    RouterGateway& router_;
    TimeProvider& clock_;
};

}  // namespace inerxia::application