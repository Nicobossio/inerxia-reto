#include "application/use_cases/get_plan.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

GetPlan::GetPlan(InternetPlanRepository& plans) : plans_(plans) {}

domain::InternetPlan GetPlan::operator()(const domain::PlanId& id) const {
    auto plan = plans_.find_by_id(id);
    if (!plan) {
        throw EntityNotFoundError("Plan not found");
    }
    return *plan;
}

}  // namespace inerxia::application