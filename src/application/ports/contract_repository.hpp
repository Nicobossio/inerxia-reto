#pragma once

#include <optional>
#include <vector>

#include "domain/contract.hpp"
#include "domain/ids.hpp"

namespace inerxia::application {

class ContractRepository {
public:
    virtual ~ContractRepository() = default;

    virtual domain::ContractId next_id() = 0;
    virtual std::optional<domain::Contract> find_by_id(const domain::ContractId&) const = 0;
    virtual void save(const domain::Contract&) = 0;
    virtual std::vector<domain::Contract> all() const = 0;
};

}  // namespace inerxia::application