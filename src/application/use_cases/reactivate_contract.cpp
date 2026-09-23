#include "application/use_cases/reactivate_contract.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

ReactivateContract::ReactivateContract(ContractRepository& contracts,
                                       SubscriberRepository& subscribers,
                                       RouterGateway& router)
    : contracts_(contracts), subscribers_(subscribers), router_(router) {}

domain::Contract ReactivateContract::operator()(
    const ReactivateContractCommand& command) const {
    auto contract = contracts_.find_by_id(command.contract_id);
    if (!contract) {
        throw EntityNotFoundError("Contract not found");
    }
    const auto subscriber = subscribers_.find_by_id(contract->subscriber_id());
    if (!subscriber) {
        throw EntityNotFoundError("Subscriber not found");
    }

    contract->reactivate();
    contracts_.save(*contract);
    router_.enableUser(contract->id(), subscriber->static_ip());
    return *contract;
}

}  // namespace inerxia::application