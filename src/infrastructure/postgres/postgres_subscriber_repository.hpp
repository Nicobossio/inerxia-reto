#pragma once

#include <optional>
#include <vector>

#include "application/ports/subscriber_repository.hpp"
#include "infrastructure/postgres/pg_connection.hpp"

namespace inerxia::infrastructure::postgres {

class PostgresSubscriberRepository : public application::SubscriberRepository {
public:
    explicit PostgresSubscriberRepository(PostgresPool& pool) : pool_(pool) {}

    domain::SubscriberId next_id() override;
    std::optional<domain::Subscriber> find_by_id(const domain::SubscriberId& id) const override;
    std::vector<domain::Subscriber> find_all() const override;
    void save(const domain::Subscriber& subscriber) override;

private:
    PostgresPool& pool_;
};

}  // namespace inerxia::infrastructure::postgres