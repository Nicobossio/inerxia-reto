#include "api/subscriber_controller.hpp"

#include "api/dto.hpp"
#include "domain/ip_address.hpp"

namespace inerxia::api {

SubscriberController::SubscriberController(application::CreateSubscriber& create,
                                           application::GetSubscriber& get)
    : create_(create), get_(get) {}

void SubscriberController::register_routes(httplib::Server& server) {
    server.Post("/api/subscribers", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create(req, res);
    });
    server.Get("/api/subscribers/([^/]+)",
               [this](const httplib::Request& req, httplib::Response& res) {
                   handle_get(req, res);
               });
}

void SubscriberController::handle_create(const httplib::Request& request,
                                         httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto body = parse_body(request);
            const auto dto = CreateSubscriberRequest{
                body.at("name").get<std::string>(),
                body.at("static_ip").get<std::string>()};
            const auto subscriber = create_(
                application::CreateSubscriberCommand{dto.name, domain::IPAddress{dto.static_ip}});
            response.status = 201;
            response.set_content(to_json(to_view(subscriber)).dump(), "application/json");
        },
        response);
}

void SubscriberController::handle_get(const httplib::Request& request,
                                      httplib::Response& response) const {
    run_and_handle(
        [&] {
            const auto subscriber = get_(domain::SubscriberId{std::string{request.matches[1]}});
            response.status = 200;
            response.set_content(to_json(to_view(subscriber)).dump(), "application/json");
        },
        response);
}

}  // namespace inerxia::api