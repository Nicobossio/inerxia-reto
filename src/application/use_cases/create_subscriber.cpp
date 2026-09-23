#include "application/use_cases/create_subscriber.hpp"

namespace inerxia::application {

CreateSubscriber::CreateSubscriber(SubscriberRepository& subscribers)
    : subscribers_(subscribers) {}

domain::Subscriber CreateSubscriber::operator()(const CreateSubscriberCommand& command) const {
    domain::Subscriber subscriber{subscribers_.next_id(), command.name, command.static_ip};
    subscribers_.save(subscriber);
    return subscriber;
}

}  // namespace inerxia::application