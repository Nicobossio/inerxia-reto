#include "infrastructure/postgres/postgres_contract_repository.hpp"

#include <map>
#include <string>
#include <utility>

#include "domain/contract_status.hpp"
#include "domain/money.hpp"
#include "domain/payment.hpp"
#include "domain/speed_profile.hpp"
#include "infrastructure/postgres/pg_row.hpp"
#include "infrastructure/postgres/pg_utils.hpp"

namespace inerxia::infrastructure::postgres {
namespace {

std::optional<std::string> to_reason_text(std::optional<domain::SuspensionReason> reason) {
    if (!reason.has_value()) {
        return std::nullopt;
    }
    return *reason == domain::SuspensionReason::Manual ? std::string{"manual"}
                                                      : std::string{"overdue"};
}

std::optional<domain::SuspensionReason> from_reason_text(
    const std::optional<std::string>& text) {
    if (!text.has_value()) {
        return std::nullopt;
    }
    if (*text == "manual") {
        return domain::SuspensionReason::Manual;
    }
    if (*text == "overdue") {
        return domain::SuspensionReason::Overdue;
    }
    throw PostgresError("Invalid suspended_reason value in the database: '" + *text + "'");
}

domain::Payment to_payment(const PgRow& row) {
    return domain::Payment{domain::PaymentId{row.required_text(0)},
                           domain::ContractId{row.required_text(1)},
                           domain::Money::from_cents(row.bigint(2)),
                           from_sql_date(row.required_text(3))};
}

domain::Contract to_contract(const PgRow& row,
                             std::vector<domain::Payment> payments) {
    return domain::Contract::rehydrate(
        domain::ContractId{row.required_text(0)},
        domain::SubscriberId{row.required_text(1)},
        domain::PlanId{row.required_text(2)},
        domain::SpeedProfile{row.integer(3), row.integer(4)},
        from_sql_date(row.required_text(5)), from_sql_date(row.required_text(6)),
        domain::Money::from_cents(row.bigint(7)),
        from_reason_text(row.optional_text(8)), std::move(payments));
}

constexpr auto kContractColumns =
    "id, subscriber_id, plan_id, download_mbps, upload_mbps, billing_start, due_date, "
    "price_per_period_cents, suspended_reason";

}  // namespace

domain::ContractId PostgresContractRepository::next_id() {
    return domain::ContractId{next_uuid_v4()};
}

std::optional<domain::Contract> PostgresContractRepository::find_by_id(
    const domain::ContractId& id) const {
    auto connection = pool_.acquire();
    connection->prepare("contract_select_by_id",
                       std::string{"SELECT "} + kContractColumns +
                           " FROM contracts WHERE id = $1");
    const PgResult contract_result =
        connection->exec_prepared("contract_select_by_id", {std::string{id.value()}});
    if (contract_result.row_count() == 0) {
        return std::nullopt;
    }

    connection->prepare("contract_select_payments",
                       "SELECT id, contract_id, amount_cents, registered_on "
                       "FROM payments WHERE contract_id = $1 "
                       "ORDER BY registered_on, id");
    const PgResult payment_result = connection->exec_prepared(
        "contract_select_payments", {std::string{id.value()}});

    std::vector<domain::Payment> payments;
    payments.reserve(static_cast<std::size_t>(payment_result.row_count()));
    for (int row = 0; row < payment_result.row_count(); ++row) {
        payments.push_back(to_payment(PgRow{payment_result, row}));
    }
    return to_contract(PgRow{contract_result, 0}, std::move(payments));
}

void PostgresContractRepository::save(const domain::Contract& contract) {
    auto connection = pool_.acquire();
    connection->prepare(
        "contract_upsert",
        "INSERT INTO contracts (id, subscriber_id, plan_id, download_mbps, upload_mbps, "
        "billing_start, due_date, price_per_period_cents, suspended_reason) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
        "ON CONFLICT (id) DO UPDATE SET subscriber_id = EXCLUDED.subscriber_id, "
        "plan_id = EXCLUDED.plan_id, download_mbps = EXCLUDED.download_mbps, "
        "upload_mbps = EXCLUDED.upload_mbps, billing_start = EXCLUDED.billing_start, "
        "due_date = EXCLUDED.due_date, "
        "price_per_period_cents = EXCLUDED.price_per_period_cents, "
        "suspended_reason = EXCLUDED.suspended_reason");
    connection->prepare("contract_delete_payments",
                        "DELETE FROM payments WHERE contract_id = $1");
    connection->prepare(
        "contract_insert_payment",
        "INSERT INTO payments (id, contract_id, amount_cents, registered_on) "
        "VALUES ($1, $2, $3, $4)");

    PgTransaction transaction{*connection};
    connection->exec_prepared(
        "contract_upsert",
        {std::string{contract.id().value()},
         std::string{contract.subscriber_id().value()},
         std::string{contract.plan_id().value()},
         std::to_string(contract.speed_profile().download_mbps()),
         std::to_string(contract.speed_profile().upload_mbps()),
         to_sql_date(contract.billing_start()), to_sql_date(contract.due_date()),
         std::to_string(contract.price_per_period().cents()),
         to_reason_text(contract.suspended_reason())});
    connection->exec_prepared("contract_delete_payments",
                              {std::string{contract.id().value()}});
    for (const auto& payment : contract.payments()) {
        connection->exec_prepared(
            "contract_insert_payment",
            {std::string{payment.id().value()},
             std::string{payment.contract_id().value()},
             std::to_string(payment.amount().cents()),
             to_sql_date(payment.registered_on())});
    }
    transaction.commit();
}

std::vector<domain::Contract> PostgresContractRepository::all() const {
    auto connection = pool_.acquire();
    connection->prepare("contract_select_all",
                        std::string{"SELECT "} + kContractColumns +
                            " FROM contracts ORDER BY billing_start, id");
    connection->prepare("contract_select_all_payments",
                        "SELECT id, contract_id, amount_cents, registered_on "
                        "FROM payments ORDER BY contract_id, registered_on, id");

    const PgResult contract_result =
        connection->exec_prepared("contract_select_all", {});
    const PgResult payment_result =
        connection->exec_prepared("contract_select_all_payments", {});

    std::map<std::string, std::vector<domain::Payment>> payments_by_contract;
    for (int row = 0; row < payment_result.row_count(); ++row) {
        const PgRow pg_row{payment_result, row};
        payments_by_contract[pg_row.required_text(1)].push_back(to_payment(pg_row));
    }

    std::vector<domain::Contract> contracts;
    contracts.reserve(static_cast<std::size_t>(contract_result.row_count()));
    for (int row = 0; row < contract_result.row_count(); ++row) {
        const PgRow pg_row{contract_result, row};
        const auto contract_id = pg_row.required_text(0);
        auto found = payments_by_contract.find(contract_id);
        std::vector<domain::Payment> payments =
            found != payments_by_contract.end()
                ? std::move(found->second)
                : std::vector<domain::Payment>{};
        contracts.push_back(to_contract(pg_row, std::move(payments)));
    }
    return contracts;
}

}  // namespace inerxia::infrastructure::postgres