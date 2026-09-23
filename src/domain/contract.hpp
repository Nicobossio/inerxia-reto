#pragma once

#include <chrono>
#include <optional>
#include <variant>
#include <vector>

#include "domain/contract_status.hpp"
#include "domain/events.hpp"
#include "domain/ids.hpp"
#include "domain/money.hpp"
#include "domain/payment.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::domain {

using ContractEvent =
    std::variant<PaymentRegisteredEvent, ContractSuspendedEvent,
                 ContractReactivatedEvent>;

class Contract {
public:
    Contract(ContractId id, SubscriberId subscriber_id, PlanId plan_id,
             SpeedProfile speed_profile, std::chrono::year_month_day billing_start,
             std::chrono::year_month_day due_date, Money price_per_period);

    [[nodiscard]] const ContractId& id() const noexcept { return id_; }
    [[nodiscard]] const SubscriberId& subscriber_id() const noexcept {
        return subscriber_id_;
    }
    [[nodiscard]] const PlanId& plan_id() const noexcept { return plan_id_; }
    [[nodiscard]] const SpeedProfile& speed_profile() const noexcept {
        return speed_profile_;
    }
    [[nodiscard]] std::chrono::year_month_day billing_start() const noexcept {
        return billing_start_;
    }
    [[nodiscard]] std::chrono::year_month_day due_date() const noexcept {
        return due_date_;
    }
    [[nodiscard]] Money price_per_period() const noexcept { return price_per_period_; }
    [[nodiscard]] bool is_suspended() const noexcept {
        return suspended_reason_.has_value();
    }

    void change_speed_profile(SpeedProfile profile);

    void suspend(SuspensionReason reason);
    void reactivate();
    void auto_suspend(std::chrono::year_month_day on_date);

    void attach_payment(Payment payment);

    [[nodiscard]] Money balance_as_of(std::chrono::year_month_day date) const;
    [[nodiscard]] bool is_overdue(std::chrono::year_month_day date) const;
    [[nodiscard]] ContractStatus status_as_of(std::chrono::year_month_day date) const;

    [[nodiscard]] std::vector<ContractEvent> take_events();

private:
    [[nodiscard]] Money paid_as_of(std::chrono::year_month_day date) const;

    ContractId id_;
    SubscriberId subscriber_id_;
    PlanId plan_id_;
    SpeedProfile speed_profile_;
    std::chrono::year_month_day billing_start_;
    std::chrono::year_month_day due_date_;
    Money price_per_period_;
    std::optional<SuspensionReason> suspended_reason_;
    std::vector<Payment> payments_;
    std::vector<ContractEvent> events_;
};

}  // namespace inerxia::domain