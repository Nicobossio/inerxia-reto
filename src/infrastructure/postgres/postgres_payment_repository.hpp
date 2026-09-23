#pragma once

#include "application/ports/payment_repository.hpp"
#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

class PostgresPaymentRepository : public application::PaymentRepository {
public:
    explicit PostgresPaymentRepository(PostgresPool& pool) : pool_(pool) {}

    domain::PaymentId next_id() override;
    void save(const domain::Payment& payment) override;

private:
    PostgresPool& pool_;
};

}  // namespace inerxia::infrastructure::postgres