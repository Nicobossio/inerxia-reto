#include "infrastructure/postgres/postgres_payment_repository.hpp"

#include <string>

#include "infrastructure/postgres/pg_utils.hpp"

namespace inerxia::infrastructure::postgres {

domain::PaymentId PostgresPaymentRepository::next_id() {
    return domain::PaymentId{next_uuid_v4()};
}

void PostgresPaymentRepository::save(const domain::Payment& payment) {
    auto connection = pool_.acquire();
    connection->prepare(
        "payment_upsert",
        "INSERT INTO payments (id, contract_id, amount_cents, registered_on) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (id) DO UPDATE SET contract_id = EXCLUDED.contract_id, "
        "amount_cents = EXCLUDED.amount_cents, registered_on = EXCLUDED.registered_on");
    connection->exec_prepared("payment_upsert",
                              {std::string{payment.id().value()},
                               std::string{payment.contract_id().value()},
                               std::to_string(payment.amount().cents()),
                               to_sql_date(payment.registered_on())});
}

}  // namespace inerxia::infrastructure::postgres