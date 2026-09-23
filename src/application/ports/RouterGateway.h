#pragma once

#include "domain/ids.hpp"
#include "domain/ip_address.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::application {

class RouterGateway {
public:
    virtual ~RouterGateway() = default;

    virtual void enableUser(const domain::ContractId&, const domain::IPAddress&) = 0;
    virtual void disableUser(const domain::ContractId&, const domain::IPAddress&) = 0;
    virtual void changeSpeedProfile(const domain::ContractId&, const domain::IPAddress&,
                                    const domain::SpeedProfile&) = 0;
};

}  // namespace inerxia::application