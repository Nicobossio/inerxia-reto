#pragma once

#include <optional>

#include "domain/ids.hpp"
#include "domain/internet_plan.hpp"

namespace inerxia::application {

class InternetPlanRepository {
public:
    virtual ~InternetPlanRepository() = default;

    virtual std::optional<domain::InternetPlan> find_by_id(const domain::PlanId&) const = 0;
    virtual void save(const domain::InternetPlan&) = 0;
};

}  // namespace inerxia::application