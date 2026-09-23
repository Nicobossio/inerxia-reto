#pragma once

#include <string>

#include "application/ports/internet_plan_repository.hpp"
#include "domain/internet_plan.hpp"
#include "domain/money.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::application {

struct CreatePlanCommand {
    std::string name;
    domain::SpeedProfile speed;
    domain::Money monthly_price;
};

class CreatePlan {
public:
    explicit CreatePlan(InternetPlanRepository&);

    domain::InternetPlan operator()(const CreatePlanCommand&) const;

private:
    InternetPlanRepository& plans_;
};

}  // namespace inerxia::application