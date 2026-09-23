#pragma once

#include "application/ports/contract_repository.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/subscriber_repository.hpp"
#include "domain/ids.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::application {

struct ChangeSpeedProfileCommand {
    domain::ContractId contract_id;
    domain::SpeedProfile new_profile;
};

class ChangeSpeedProfile {
public:
    ChangeSpeedProfile(ContractRepository&, SubscriberRepository&, RouterGateway&);

    domain::Contract operator()(const ChangeSpeedProfileCommand&) const;

private:
    ContractRepository& contracts_;
    SubscriberRepository& subscribers_;
    RouterGateway& router_;
};

}  // namespace inerxia::application