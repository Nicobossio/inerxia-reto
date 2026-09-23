#include "application/use_cases/create_plan.hpp"

namespace inerxia::application {

CreatePlan::CreatePlan(InternetPlanRepository& plans) : plans_(plans) {}

domain::InternetPlan CreatePlan::operator()(const CreatePlanCommand& command) const {
    domain::InternetPlan plan{plans_.next_id(), command.name, command.speed,
                              command.monthly_price};
    plans_.save(plan);
    return plan;
}

}  // namespace inerxia::application