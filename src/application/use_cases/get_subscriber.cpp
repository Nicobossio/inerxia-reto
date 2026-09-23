#include "application/use_cases/get_subscriber.hpp"

#include "application/application_error.hpp"

namespace inerxia::application {

GetSubscriber::GetSubscriber(SubscriberRepository& subscribers) : subscribers_(subscribers) {}

domain::Subscriber GetSubscriber::operator()(const domain::SubscriberId& id) const {
    auto subscriber = subscribers_.find_by_id(id);
    if (!subscriber) {
        throw EntityNotFoundError("Subscriber not found");
    }
    return *subscriber;
}

}  // namespace inerxia::application