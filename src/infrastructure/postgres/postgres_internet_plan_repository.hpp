#pragma once

#include <optional>

#include "application/ports/internet_plan_repository.hpp"
#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

class PostgresInternetPlanRepository : public application::InternetPlanRepository {
public:
    explicit PostgresInternetPlanRepository(PostgresPool& pool) : pool_(pool) {}

    domain::PlanId next_id() override;
    std::optional<domain::InternetPlan> find_by_id(const domain::PlanId& id) const override;
    void save(const domain::InternetPlan& plan) override;

private:
    PostgresPool& pool_;
};

}  // namespace inerxia::infrastructure::postgres