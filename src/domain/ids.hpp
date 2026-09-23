#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "domain/domain_error.hpp"

namespace inerxia::domain {

template <typename Tag>
class Id {
public:
    explicit Id(std::string value) : value_(std::move(value)) {
        if (value_.empty()) {
            throw DomainError("Id cannot be empty");
        }
    }

    [[nodiscard]] std::string_view value() const noexcept { return value_; }

    friend bool operator==(const Id&, const Id&) = default;

private:
    std::string value_;
};

using SubscriberId = Id<class SubscriberTag>;
using PlanId = Id<class PlanTag>;
using ContractId = Id<class ContractTag>;
using PaymentId = Id<class PaymentTag>;

}  // namespace inerxia::domain