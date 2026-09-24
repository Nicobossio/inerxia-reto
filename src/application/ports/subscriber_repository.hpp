#pragma once

#include <optional>
#include <vector>

#include "domain/ids.hpp"
#include "domain/subscriber.hpp"

namespace inerxia::application {

class SubscriberRepository {
public:
    virtual ~SubscriberRepository() = default;

    virtual domain::SubscriberId next_id() = 0;
    virtual std::optional<domain::Subscriber> find_by_id(const domain::SubscriberId&) const = 0;
    virtual std::vector<domain::Subscriber> find_all() const = 0;
    virtual void save(const domain::Subscriber&) = 0;
};

}  // namespace inerxia::application