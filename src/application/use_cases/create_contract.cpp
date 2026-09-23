#include "application/use_cases/create_contract.hpp"

#include <utility>

#include "application/application_error.hpp"

namespace inerxia::application {

CreateContract::CreateContract(ContractRepository& contracts,
                               SubscriberRepository& subscribers,
                               InternetPlanRepository& plans)
    : contracts_(contracts), subscribers_(subscribers), plans_(plans) {}

domain::Contract CreateContract::operator()(const CreateContractCommand& command) const {
    if (!subscribers_.find_by_id(command.subscriber_id)) {
        throw EntityNotFoundError("Subscriber not found");
    }
    const auto plan = plans_.find_by_id(command.plan_id);
    if (!plan) {
        throw EntityNotFoundError("Internet plan not found");
    }

    domain::Contract contract{contracts_.next_id(), command.subscriber_id,
                              command.plan_id, plan->speed(), command.billing_start,
                              command.due_date, plan->monthly_price()};
    contracts_.save(contract);
    return contract;
}

}  // namespace inerxia::application