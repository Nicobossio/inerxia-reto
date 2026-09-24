#include "application/use_cases/list_subscribers.hpp"

namespace inerxia::application {

ListSubscribers::ListSubscribers(SubscriberRepository& subscribers) : subscribers_(subscribers) {}

std::vector<domain::Subscriber> ListSubscribers::operator()() const {
    return subscribers_.find_all();
}

}  // namespace inerxia::application