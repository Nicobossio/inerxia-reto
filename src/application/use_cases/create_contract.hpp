#pragma once

#include <chrono>

#include "application/ports/contract_repository.hpp"
#include "application/ports/internet_plan_repository.hpp"
#include "application/ports/subscriber_repository.hpp"
#include "domain/contract.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

struct CreateContractCommand {
    domain::SubscriberId subscriber_id;
    domain::PlanId plan_id;
    std::chrono::year_month_day billing_start;
    std::chrono::year_month_day due_date;
};

class CreateContract {
public:
    CreateContract(ContractRepository&, SubscriberRepository&, InternetPlanRepository&);

    domain::Contract operator()(const CreateContractCommand&) const;

private:
    ContractRepository& contracts_;
    SubscriberRepository& subscribers_;
    InternetPlanRepository& plans_;
};

}  // namespace inerxia::application