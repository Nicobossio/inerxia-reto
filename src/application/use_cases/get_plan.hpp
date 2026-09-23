#pragma once

#include "application/ports/internet_plan_repository.hpp"
#include "domain/ids.hpp"
#include "domain/internet_plan.hpp"

namespace inerxia::application {

class GetPlan {
public:
    explicit GetPlan(InternetPlanRepository&);

    domain::InternetPlan operator()(const domain::PlanId&) const;

private:
    InternetPlanRepository& plans_;
};

}  // namespace inerxia::application