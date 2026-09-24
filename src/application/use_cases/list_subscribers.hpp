#pragma once

#include <vector>

#include "application/ports/subscriber_repository.hpp"
#include "domain/subscriber.hpp"

namespace inerxia::application {

// Read-model listing of every subscriber: the operator's inventory.
class ListSubscribers {
public:
    explicit ListSubscribers(SubscriberRepository& subscribers);

    std::vector<domain::Subscriber> operator()() const;

private:
    SubscriberRepository& subscribers_;
};

}  // namespace inerxia::application