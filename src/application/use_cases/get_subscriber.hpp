#pragma once

#include "application/ports/subscriber_repository.hpp"
#include "domain/ids.hpp"
#include "domain/subscriber.hpp"

namespace inerxia::application {

class GetSubscriber {
public:
    explicit GetSubscriber(SubscriberRepository&);

    domain::Subscriber operator()(const domain::SubscriberId&) const;

private:
    SubscriberRepository& subscribers_;
};

}  // namespace inerxia::application