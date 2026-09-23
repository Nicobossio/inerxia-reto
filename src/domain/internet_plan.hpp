#pragma once

#include <string>

#include "domain/ids.hpp"
#include "domain/money.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::domain {

class InternetPlan {
public:
    InternetPlan(PlanId id, std::string name, SpeedProfile speed, Money monthly_price);

    [[nodiscard]] const PlanId& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const SpeedProfile& speed() const noexcept { return speed_; }
    [[nodiscard]] Money monthly_price() const noexcept { return monthly_price_; }

private:
    PlanId id_;
    std::string name_;
    SpeedProfile speed_;
    Money monthly_price_;
};

}  // namespace inerxia::domain