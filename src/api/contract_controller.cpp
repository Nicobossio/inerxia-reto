#include "api/contract_controller.hpp"

#include "api/dto.hpp"
#include "domain/ids.hpp"
#include "domain/money.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::api {
namespace {

inline domain::ContractId path_id(const httplib::Request& request) {
    return domain::ContractId{std::string{request.matches[1]}};
}

inline void set_contract_response(const application::GetContract& get,
                                  const domain::ContractId& id, httplib::Response& response,
                                  int status = 200) {
    const auto snapshot = get(id);
    response.status = status;
    response.set_content(to_json(to_view(snapshot.contract, snapshot.status)).dump(),
                         "application/json");
}

}  // namespace

ContractController::ContractController(
    application::CreateContract& create, application::GetContract& get,
    application::UpdateContract& update, application::SuspendContract& suspend,
    application::ReactivateContract& reactivate, application::ChangeSpeedProfile& change_speed,
    application::RegisterPayment& register_payment,
    application::EvaluateExpiredContracts& evaluate)
    : create_(create),
      get_(get),
      update_(update),
      suspend_(suspend),
      reactivate_(reactivate),
      change_speed_(change_speed),
      register_payment_(register_payment),
      evaluate_(evaluate) {}

void ContractController::register_routes(httplib::Server& server) {
    // More specific paths are registered before the generic {id} captures so
    // httplib's first-match semantics resolve them correctly.
    server.Post("/api/sweep/expired",
                [this](const httplib::Request& req, httplib::Response& res) {
                    handle_sweep(req, res);
                });
    server.Post("/api/contracts/([^/]+)/payments",
                [this](const httplib::Request& req, httplib::Response& res) {
                    handle_register_payment(req, res);
                });
    server.Patch("/api/contracts/([^/]+)/speed-profile",
                 [this](const httplib::Request& req, httplib::Response& res) {
                     handle_change_speed(req, res);
                 });
    server.Post("/api/contracts/([^/]+)/suspend",
                [this](const httplib::Request& req, httplib::Response& res) {
                    handle_suspend(req, res);
                });
    server.Post("/api/contracts/([^/]+)/reactivate",
                [this](const httplib::Request& req, httplib::Response& res) {
                    handle_reactivate(req, res);
                });
    server.Put("/api/contracts/([^/]+)",
               [this](const httplib::Request& req, httplib::Response& res) {
                   handle_update(req, res);
               });
    server.Post("/api/contracts", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create(req, res);
    });
    server.Get("/api/contracts/([^/]+)",
               [this](const httplib::Request& req, httplib::Response& res) {
                   handle_get(req, res);
               });
}

void ContractController::handle_create(const httplib::Request& request,
                                       httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const auto dto = CreateContractRequest{
                body.at("subscriber_id").get<std::string>(),
                body.at("plan_id").get<std::string>(),
                body.at("billing_start").get<std::string>(),
                body.at("due_date").get<std::string>()};
            const auto contract = create_(application::CreateContractCommand{
                domain::SubscriberId{dto.subscriber_id}, domain::PlanId{dto.plan_id},
                required_iso_date(body, "billing_start"), required_iso_date(body, "due_date")});
            response.status = 201;
            set_contract_response(get_, contract.id(), response, 201);
        },
        response);
}

void ContractController::handle_get(const httplib::Request& request,
                                    httplib::Response& response) const {
    run_and_handle(
        [&] { set_contract_response(get_, path_id(request), response); }, response);
}

void ContractController::handle_update(const httplib::Request& request,
                                       httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const auto contract = update_(
                application::UpdateContractCommand{path_id(request),
                                                   required_iso_date(body, "due_date")});
            set_contract_response(get_, contract.id(), response);
        },
        response);
}

void ContractController::handle_suspend(const httplib::Request& request,
                                        httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto contract = suspend_(application::SuspendContractCommand{path_id(request)});
            set_contract_response(get_, contract.id(), response);
        },
        response);
}

void ContractController::handle_reactivate(const httplib::Request& request,
                                           httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto contract =
                reactivate_(application::ReactivateContractCommand{path_id(request)});
            set_contract_response(get_, contract.id(), response);
        },
        response);
}

void ContractController::handle_change_speed(const httplib::Request& request,
                                             httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const auto contract =
                change_speed_(application::ChangeSpeedProfileCommand{
                    path_id(request),
                    domain::SpeedProfile{body.at("download_mbps").get<int>(),
                                         body.at("upload_mbps").get<int>()}});
            set_contract_response(get_, contract.id(), response);
        },
        response);
}

void ContractController::handle_register_payment(const httplib::Request& request,
                                                 httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const auto payment = register_payment_(application::RegisterPaymentCommand{
                path_id(request), domain::Money::from_cents(body.at("amount_cents").get<std::int64_t>()),
                required_iso_date(body, "paid_on")});
            response.status = 201;
            response.set_content(to_json(to_view(payment)).dump(), "application/json");
        },
        response);
}

void ContractController::handle_sweep(const httplib::Request&,
                                      httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto suspended = evaluate_();
            nlohmann::json body = nlohmann::json::array();
            for (const auto& contract_id : suspended) {
                body.push_back(std::string{contract_id.value()});
            }
            response.status = 200;
            response.set_content(nlohmann::json{{"suspended_contract_ids", body}}.dump(),
                                 "application/json");
        },
        response);
}

}  // namespace inerxia::api