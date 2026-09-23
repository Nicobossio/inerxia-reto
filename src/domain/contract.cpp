#include "domain/contract.hpp"

#include <utility>

#include "domain/domain_error.hpp"

namespace inerxia::domain {
namespace {

using Days = std::chrono::days;
using SysDays = std::chrono::sys_days;

bool is_valid_date(std::chrono::year_month_day date) { return date.ok(); }

}  // namespace

Contract::Contract(ContractId id, SubscriberId subscriber_id, PlanId plan_id,
                   SpeedProfile speed_profile,
                   std::chrono::year_month_day billing_start,
                   std::chrono::year_month_day due_date, Money price_per_period)
    : id_(std::move(id)),
      subscriber_id_(std::move(subscriber_id)),
      plan_id_(std::move(plan_id)),
      speed_profile_(std::move(speed_profile)),
      billing_start_(billing_start),
      due_date_(due_date),
      price_per_period_(price_per_period) {
    if (!is_valid_date(billing_start_)) {
        throw DomainError("Contract billing start date must be a valid calendar date");
    }
    if (!is_valid_date(due_date_)) {
        throw DomainError("Contract due date must be a valid calendar date");
    }
    if (billing_start_ >= due_date_) {
        throw DomainError("Contract due date must be after billing start date");
    }
    if (!price_per_period_.is_positive()) {
        throw DomainError("Contract price per period must be positive");
    }
}

void Contract::change_speed_profile(SpeedProfile profile) {
    speed_profile_ = std::move(profile);
}

void Contract::suspend(SuspensionReason reason) {
    if (is_suspended()) {
        throw DomainError("Contract is already suspended");
    }
    suspended_reason_ = reason;
    events_.push_back(ContractSuspendedEvent{id_});
}

void Contract::reactivate() {
    if (!is_suspended()) {
        throw DomainError("Only a suspended contract can be reactivated");
    }
    suspended_reason_.reset();
    events_.push_back(ContractReactivatedEvent{id_});
}

void Contract::auto_suspend(std::chrono::year_month_day on_date) {
    if (is_suspended()) {
        throw DomainError("Contract is already suspended");
    }
    if (!is_overdue(on_date)) {
        throw DomainError("Contract is not overdue; automatic suspension not allowed");
    }
    suspend(SuspensionReason::Overdue);
}

void Contract::attach_payment(Payment payment) {
    if (payment.contract_id() != id_) {
        throw DomainError("Payment must belong to the contract it is attached to");
    }
    payments_.push_back(std::move(payment));
    const auto& last = payments_.back();
    events_.push_back(
        PaymentRegisteredEvent{id_, last.amount(), last.registered_on()});

    if (suspended_reason_ == SuspensionReason::Overdue &&
        !balance_as_of(last.registered_on()).is_positive()) {
        suspended_reason_.reset();
        events_.push_back(ContractReactivatedEvent{id_});
    }
}

Money Contract::paid_as_of(std::chrono::year_month_day date) const {
    Money paid{0};
    for (const auto& payment : payments_) {
        if (payment.registered_on() <= date) {
            paid += payment.amount();
        }
    }
    return paid;
}

Money Contract::balance_as_of(std::chrono::year_month_day date) const {
    return price_per_period_ - paid_as_of(date);
}

bool Contract::is_overdue(std::chrono::year_month_day date) const {
    if (!is_valid_date(date)) {
        throw DomainError("Reference date must be a valid calendar date");
    }
    if (SysDays{date} <= SysDays{due_date_}) {
        return false;
    }
    return balance_as_of(date).is_positive();
}

ContractStatus Contract::status_as_of(std::chrono::year_month_day date) const {
    if (is_suspended()) {
        return ContractStatus::Suspended;
    }
    if (is_overdue(date)) {
        return ContractStatus::Overdue;
    }
    return ContractStatus::Active;
}

std::vector<ContractEvent> Contract::take_events() {
    return std::exchange(events_, {});
}

}  // namespace inerxia::domain