#include "api/plan_controller.hpp"

#include "api/dto.hpp"
#include "domain/money.hpp"
#include "domain/speed_profile.hpp"

namespace inerxia::api {

PlanController::PlanController(application::CreatePlan& create, application::GetPlan& get)
    : create_(create), get_(get) {}

void PlanController::register_routes(httplib::Server& server) {
    server.Post("/api/plans", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create(req, res);
    });
    server.Get("/api/plans/([^/]+)",
               [this](const httplib::Request& req, httplib::Response& res) {
                   handle_get(req, res);
               });
}

void PlanController::handle_create(const httplib::Request& request,
                                   httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const auto dto = CreatePlanRequest{
                body.at("name").get<std::string>(),
                body.at("download_mbps").get<int>(),
                body.at("upload_mbps").get<int>(),
                body.at("monthly_price_cents").get<std::int64_t>()};
            const auto plan =
                create_(application::CreatePlanCommand{
                    dto.name, domain::SpeedProfile{dto.download_mbps, dto.upload_mbps},
                    domain::Money::from_cents(dto.monthly_price_cents)});
            response.status = 201;
            response.set_content(to_json(to_view(plan)).dump(), "application/json");
        },
        response);
}

void PlanController::handle_get(const httplib::Request& request,
                                httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto plan = get_(domain::PlanId{std::string{request.matches[1]}});
            response.status = 200;
            response.set_content(to_json(to_view(plan)).dump(), "application/json");
        },
        response);
}

}  // namespace inerxia::api