#pragma once

#include <string>

#include "application/ports/subscriber_repository.hpp"
#include "domain/ip_address.hpp"
#include "domain/subscriber.hpp"

namespace inerxia::application {

struct CreateSubscriberCommand {
    std::string name;
    domain::IPAddress static_ip;
};

class CreateSubscriber {
public:
    explicit CreateSubscriber(SubscriberRepository&);

    domain::Subscriber operator()(const CreateSubscriberCommand&) const;

private:
    SubscriberRepository& subscribers_;
};

}  // namespace inerxia::application