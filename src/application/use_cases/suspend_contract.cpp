#include "application/use_cases/suspend_contract.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

SuspendContract::SuspendContract(ContractRepository& contracts,
                                 SubscriberRepository& subscribers, RouterGateway& router)
    : contracts_(contracts), subscribers_(subscribers), router_(router) {}

domain::Contract SuspendContract::operator()(const SuspendContractCommand& command) const {
    auto contract = contracts_.find_by_id(command.contract_id);
    if (!contract) {
        throw EntityNotFoundError("Contract not found");
    }
    const auto subscriber = subscribers_.find_by_id(contract->subscriber_id());
    if (!subscriber) {
        throw EntityNotFoundError("Subscriber not found");
    }

    contract->suspend(domain::SuspensionReason::Manual);
    contracts_.save(*contract);
    router_.disableUser(contract->id(), subscriber->static_ip());
    return *contract;
}

}  // namespace inerxia::application