#pragma once

#include <optional>
#include <vector>

#include "application/ports/contract_repository.hpp"
#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

class PostgresContractRepository : public application::ContractRepository {
public:
    explicit PostgresContractRepository(PostgresPool& pool) : pool_(pool) {}

    domain::ContractId next_id() override;
    std::optional<domain::Contract> find_by_id(const domain::ContractId& id) const override;
    void save(const domain::Contract& contract) override;
    std::vector<domain::Contract> all() const override;

private:
    PostgresPool& pool_;
};

}  // namespace inerxia::infrastructure::postgres