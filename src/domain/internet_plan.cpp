#include "domain/internet_plan.hpp"

#include <string>
#include <utility>

#include "domain/domain_error.hpp"

namespace inerxia::domain {
namespace {

void validate_name(const std::string& name, const std::string& what) {
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        throw DomainError(what + " must not be empty");
    }
}

}  // namespace

InternetPlan::InternetPlan(PlanId id, std::string name, SpeedProfile speed,
                           Money monthly_price)
    : id_(std::move(id)),
      name_(std::move(name)),
      speed_(std::move(speed)),
      monthly_price_(monthly_price) {
    validate_name(name_, "Plan name");
    if (!monthly_price_.is_positive()) {
        throw DomainError("Plan monthly price must be positive");
    }
}

}  // namespace inerxia::domain