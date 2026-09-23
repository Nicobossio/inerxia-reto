#include "infrastructure/postgres/postgres_internet_plan_repository.hpp"

#include <string>
#include <utility>

#include "domain/money.hpp"
#include "domain/speed_profile.hpp"
#include "infrastructure/postgres/pg_row.hpp"
#include "infrastructure/postgres/pg_utils.hpp"

namespace inerxia::infrastructure::postgres {

domain::PlanId PostgresInternetPlanRepository::next_id() {
    return domain::PlanId{next_uuid_v4()};
}

std::optional<domain::InternetPlan> PostgresInternetPlanRepository::find_by_id(
    const domain::PlanId& id) const {
    auto connection = pool_.acquire();
    connection->prepare("plan_select_by_id",
                       "SELECT id, name, download_mbps, upload_mbps, "
                       "monthly_price_cents FROM internet_plans WHERE id = $1");
    const PgResult result =
        connection->exec_prepared("plan_select_by_id", {std::string{id.value()}});
    if (result.row_count() == 0) {
        return std::nullopt;
    }
    const PgRow row{result, 0};
    return domain::InternetPlan{
        domain::PlanId{row.required_text(0)}, row.required_text(1),
        domain::SpeedProfile{row.integer(2), row.integer(3)},
        domain::Money::from_cents(row.bigint(4))};
}

void PostgresInternetPlanRepository::save(const domain::InternetPlan& plan) {
    auto connection = pool_.acquire();
    connection->prepare(
        "plan_upsert",
        "INSERT INTO internet_plans (id, name, download_mbps, upload_mbps, "
        "monthly_price_cents) VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name, "
        "download_mbps = EXCLUDED.download_mbps, upload_mbps = EXCLUDED.upload_mbps, "
        "monthly_price_cents = EXCLUDED.monthly_price_cents");
    connection->exec_prepared(
        "plan_upsert",
        {std::string{plan.id().value()}, plan.name(),
         std::to_string(plan.speed().download_mbps()),
         std::to_string(plan.speed().upload_mbps()),
         std::to_string(plan.monthly_price().cents())});
}

}  // namespace inerxia::infrastructure::postgres