#pragma once

#include <httplib.h>

#include "application/use_cases/create_subscriber.hpp"
#include "application/use_cases/get_subscriber.hpp"
#include "application/use_cases/list_subscribers.hpp"

namespace inerxia::api {

// HTTP adapter for subscriber reads/creates. Only translates between HTTP and
// application use cases; all business rules live below this layer.
class SubscriberController {
public:
    SubscriberController(application::CreateSubscriber& create,
                         application::GetSubscriber& get,
                         application::ListSubscribers& list);

    void register_routes(httplib::Server& server);

private:
    void handle_create(const httplib::Request&, httplib::Response&) const;
    void handle_get(const httplib::Request&, httplib::Response&) const;
    void handle_list_all(const httplib::Request&, httplib::Response&) const;

    application::CreateSubscriber& create_;
    application::GetSubscriber& get_;
    application::ListSubscribers& list_;
};

}  // namespace inerxia::api