#include "application/use_cases/change_speed_profile.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

ChangeSpeedProfile::ChangeSpeedProfile(ContractRepository& contracts,
                                       SubscriberRepository& subscribers,
                                       RouterGateway& router)
    : contracts_(contracts), subscribers_(subscribers), router_(router) {}

domain::Contract ChangeSpeedProfile::operator()(
    const ChangeSpeedProfileCommand& command) const {
    auto contract = contracts_.find_by_id(command.contract_id);
    if (!contract) {
        throw EntityNotFoundError("Contract not found");
    }
    const auto subscriber = subscribers_.find_by_id(contract->subscriber_id());
    if (!subscriber) {
        throw EntityNotFoundError("Subscriber not found");
    }

    const bool profile_changed = contract->speed_profile() != command.new_profile;
    contract->change_speed_profile(command.new_profile);
    contracts_.save(*contract);
    if (profile_changed) {
        router_.changeSpeedProfile(contract->id(), subscriber->static_ip(),
                                     command.new_profile);
    }
    return *contract;
}

}  // namespace inerxia::application