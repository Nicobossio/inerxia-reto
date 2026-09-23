#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/json_utils.hpp"
#include "domain/contract.hpp"
#include "domain/contract_status.hpp"
#include "domain/internet_plan.hpp"
#include "domain/payment.hpp"
#include "domain/subscriber.hpp"

namespace inerxia::api {

// Request DTOs: plain data carried by the HTTP body, fully decoupled from domain
// types. Controllers translate these into application commands.
struct CreateSubscriberRequest {
    std::string name;
    std::string static_ip;
};

struct CreatePlanRequest {
    std::string name;
    int download_mbps = 0;
    int upload_mbps = 0;
    std::int64_t monthly_price_cents = 0;
};

struct CreateContractRequest {
    std::string subscriber_id;
    std::string plan_id;
    std::string billing_start;
    std::string due_date;
};

struct UpdateContractRequest {
    std::string due_date;
};

struct ChangeSpeedProfileRequest {
    int download_mbps = 0;
    int upload_mbps = 0;
};

struct RegisterPaymentRequest {
    std::int64_t amount_cents = 0;
    std::string paid_on;
};

// Response DTOs: read-model views served over HTTP.
struct SubscriberView {
    std::string id;
    std::string name;
    std::string static_ip;
};

struct PlanView {
    std::string id;
    std::string name;
    int download_mbps = 0;
    int upload_mbps = 0;
    std::int64_t monthly_price_cents = 0;
};

struct PaymentView {
    std::string id;
    std::string contract_id;
    std::int64_t amount_cents = 0;
    std::string registered_on;
};

struct ContractView {
    std::string id;
    std::string subscriber_id;
    std::string plan_id;
    int download_mbps = 0;
    int upload_mbps = 0;
    std::string billing_start;
    std::string due_date;
    std::int64_t price_per_period_cents = 0;
    bool suspended = false;
    std::string status;
    std::vector<PaymentView> payments;
};

inline SubscriberView to_view(const domain::Subscriber& subscriber) {
    return SubscriberView{std::string{subscriber.id().value()}, subscriber.name(),
                          subscriber.static_ip().value()};
}

inline PlanView to_view(const domain::InternetPlan& plan) {
    return PlanView{std::string{plan.id().value()},         plan.name(),
                    plan.speed().download_mbps(),           plan.speed().upload_mbps(),
                    plan.monthly_price().cents()};
}

inline PaymentView to_view(const domain::Payment& payment) {
    return PaymentView{std::string{payment.id().value()},
                       std::string{payment.contract_id().value()}, payment.amount().cents(),
                       format_iso_date(payment.registered_on())};
}

inline ContractView to_view(const domain::Contract& contract, domain::ContractStatus status) {
    ContractView view;
    view.id = std::string{contract.id().value()};
    view.subscriber_id = std::string{contract.subscriber_id().value()};
    view.plan_id = std::string{contract.plan_id().value()};
    view.download_mbps = contract.speed_profile().download_mbps();
    view.upload_mbps = contract.speed_profile().upload_mbps();
    view.billing_start = format_iso_date(contract.billing_start());
    view.due_date = format_iso_date(contract.due_date());
    view.price_per_period_cents = contract.price_per_period().cents();
    view.suspended = contract.is_suspended();
    view.status = std::string{status_to_string(status)};
    for (const auto& payment : contract.payments()) {
        view.payments.push_back(to_view(payment));
    }
    return view;
}

inline nlohmann::json to_json(const SubscriberView& view) {
    return nlohmann::json{{"id", view.id},
                          {"name", view.name},
                          {"static_ip", view.static_ip}};
}

inline nlohmann::json to_json(const PlanView& view) {
    return nlohmann::json{{"id", view.id},
                          {"name", view.name},
                          {"download_mbps", view.download_mbps},
                          {"upload_mbps", view.upload_mbps},
                          {"monthly_price_cents", view.monthly_price_cents}};
}

inline nlohmann::json to_json(const PaymentView& view) {
    return nlohmann::json{{"id", view.id},
                          {"contract_id", view.contract_id},
                          {"amount_cents", view.amount_cents},
                          {"registered_on", view.registered_on}};
}

inline nlohmann::json to_json(const ContractView& view) {
    nlohmann::json body{{"id", view.id},
                        {"subscriber_id", view.subscriber_id},
                        {"plan_id", view.plan_id},
                        {"download_mbps", view.download_mbps},
                        {"upload_mbps", view.upload_mbps},
                        {"billing_start", view.billing_start},
                        {"due_date", view.due_date},
                        {"price_per_period_cents", view.price_per_period_cents},
                        {"suspended", view.suspended},
                        {"status", view.status}};
    body["payments"] = nlohmann::json::array();
    for (const auto& payment : view.payments) {
        body["payments"].push_back(to_json(payment));
    }
    return body;
}

}  // namespace inerxia::api