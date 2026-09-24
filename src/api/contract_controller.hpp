#pragma once

#include <httplib.h>

#include "application/use_cases/change_speed_profile.hpp"
#include "application/use_cases/create_contract.hpp"
#include "application/use_cases/evaluate_expired_contracts.hpp"
#include "application/use_cases/get_contract.hpp"
#include "application/use_cases/list_contracts.hpp"
#include "application/use_cases/reactivate_contract.hpp"
#include "application/use_cases/register_payment.hpp"
#include "application/use_cases/suspend_contract.hpp"
#include "application/use_cases/update_contract.hpp"

namespace inerxia::api {

// HTTP adapter for the contract lifecycle (create/query/edit/suspend/reactivate),
// speed profile changes, payment registration and the due-date sweep.
class ContractController {
public:
    ContractController(application::CreateContract& create, application::GetContract& get,
                       application::UpdateContract& update,
                       application::SuspendContract& suspend,
                       application::ReactivateContract& reactivate,
                       application::ChangeSpeedProfile& change_speed,
                       application::RegisterPayment& register_payment,
                       application::EvaluateExpiredContracts& evaluate,
                       application::ListContracts& list_contracts);

    void register_routes(httplib::Server& server);

private:
    void handle_create(const httplib::Request&, httplib::Response&) const;
    void handle_list(const httplib::Request&, httplib::Response&) const;
    void handle_get(const httplib::Request&, httplib::Response&) const;
    void handle_update(const httplib::Request&, httplib::Response&) const;
    void handle_suspend(const httplib::Request&, httplib::Response&) const;
    void handle_reactivate(const httplib::Request&, httplib::Response&) const;
    void handle_change_speed(const httplib::Request&, httplib::Response&) const;
    void handle_register_payment(const httplib::Request&, httplib::Response&) const;
    void handle_sweep(const httplib::Request&, httplib::Response&) const;

    application::CreateContract& create_;
    application::GetContract& get_;
    application::UpdateContract& update_;
    application::SuspendContract& suspend_;
    application::ReactivateContract& reactivate_;
    application::ChangeSpeedProfile& change_speed_;
    application::RegisterPayment& register_payment_;
    application::EvaluateExpiredContracts& evaluate_;
    application::ListContracts& list_contracts_;
};

}  // namespace inerxia::api