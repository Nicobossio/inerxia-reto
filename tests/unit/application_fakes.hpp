#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "application/ports/contract_repository.hpp"
#include "application/ports/internet_plan_repository.hpp"
#include "application/ports/payment_repository.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/subscriber_repository.hpp"
#include "application/ports/time_provider.hpp"

namespace inerxia::application::test {

class FakeTimeProvider final : public TimeProvider {
public:
    explicit FakeTimeProvider(std::chrono::year_month_day today) : today_(today) {}

    std::chrono::year_month_day today() const override { return today_; }

    void set_today(std::chrono::year_month_day today) { today_ = today; }

private:
    std::chrono::year_month_day today_;
};

class InMemorySubscriberRepository final : public SubscriberRepository {
public:
    std::optional<domain::Subscriber> find_by_id(const domain::SubscriberId& id) const override {
        const auto it = subscribers_.find(std::string{id.value()});
        if (it == subscribers_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void save(const domain::Subscriber& subscriber) override {
        subscribers_.insert_or_assign(std::string{subscriber.id().value()}, subscriber);
    }

private:
    std::map<std::string, domain::Subscriber> subscribers_;
};

class InMemoryInternetPlanRepository final : public InternetPlanRepository {
public:
    std::optional<domain::InternetPlan> find_by_id(const domain::PlanId& id) const override {
        const auto it = plans_.find(std::string{id.value()});
        if (it == plans_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void save(const domain::InternetPlan& plan) override {
        plans_.insert_or_assign(std::string{plan.id().value()}, plan);
    }

private:
    std::map<std::string, domain::InternetPlan> plans_;
};

class InMemoryContractRepository final : public ContractRepository {
public:
    domain::ContractId next_id() override {
        return domain::ContractId{"ct-" + std::to_string(++id_counter_)};
    }

    std::optional<domain::Contract> find_by_id(const domain::ContractId& id) const override {
        const auto it = contracts_.find(std::string{id.value()});
        if (it == contracts_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void save(const domain::Contract& contract) override {
        contracts_.insert_or_assign(std::string{contract.id().value()}, contract);
    }

    std::vector<domain::Contract> all() const override {
        std::vector<domain::Contract> result;
        result.reserve(contracts_.size());
        for (const auto& [key, value] : contracts_) {
            result.push_back(value);
        }
        return result;
    }

private:
    std::map<std::string, domain::Contract> contracts_;
    int id_counter_ = 0;
};

class InMemoryPaymentRepository final : public PaymentRepository {
public:
    domain::PaymentId next_id() override {
        return domain::PaymentId{"pay-" + std::to_string(++id_counter_)};
    }

    void save(const domain::Payment& payment) override { payments_.push_back(payment); }

    const std::vector<domain::Payment>& saved() const noexcept { return payments_; }
    std::size_t saved_count() const noexcept { return payments_.size(); }

private:
    std::vector<domain::Payment> payments_;
    int id_counter_ = 0;
};

struct RouterCall {
    enum class Kind { EnableUser, DisableUser, ChangeSpeedProfile } kind;
    std::string ip;
    std::optional<domain::SpeedProfile> speed;
};

class FakeRouterGateway final : public RouterGateway {
public:
    void enableUser(const domain::ContractId&, const domain::IPAddress& ip) override {
        calls_.push_back(RouterCall{RouterCall::Kind::EnableUser, ip.value(), std::nullopt});
    }

    void disableUser(const domain::ContractId&, const domain::IPAddress& ip) override {
        calls_.push_back(RouterCall{RouterCall::Kind::DisableUser, ip.value(), std::nullopt});
    }

    void changeSpeedProfile(const domain::ContractId&, const domain::IPAddress& ip,
                            const domain::SpeedProfile& profile) override {
        calls_.push_back(
            RouterCall{RouterCall::Kind::ChangeSpeedProfile, ip.value(), profile});
    }

    const std::vector<RouterCall>& calls() const noexcept { return calls_; }
    std::size_t calls_of(RouterCall::Kind kind) const {
        std::size_t count = 0;
        for (const auto& call : calls_) {
            if (call.kind == kind) {
                ++count;
            }
        }
        return count;
    }
    void clear_calls() { calls_.clear(); }

private:
    std::vector<RouterCall> calls_;
};

}  // namespace inerxia::application::test