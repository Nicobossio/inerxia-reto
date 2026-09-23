#include "application/use_cases/evaluate_expired_contracts.hpp"

#include "domain/contract_status.hpp"

namespace inerxia::application {

EvaluateExpiredContracts::EvaluateExpiredContracts(ContractRepository& contracts,
                                                   SubscriberRepository& subscribers,
                                                   RouterGateway& router,
                                                   TimeProvider& clock)
    : contracts_(contracts), subscribers_(subscribers), router_(router), clock_(clock) {}

std::vector<domain::ContractId> EvaluateExpiredContracts::operator()() const {
    const auto today = clock_.today();
    std::vector<domain::ContractId> suspended;
    for (auto contract : contracts_.all()) {
        if (contract.status_as_of(today) != domain::ContractStatus::Overdue) {
            continue;
        }
        const auto subscriber = subscribers_.find_by_id(contract.subscriber_id());
        if (!subscriber) {
            continue;
        }

        contract.auto_suspend(today);
        contracts_.save(contract);
        router_.disableUser(contract.id(), subscriber->static_ip());
        suspended.push_back(contract.id());
    }
    return suspended;
}

}  // namespace inerxia::application