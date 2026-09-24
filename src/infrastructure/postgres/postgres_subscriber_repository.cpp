#include "infrastructure/postgres/postgres_subscriber_repository.hpp"

#include <string>
#include <utility>

#include "infrastructure/postgres/pg_row.hpp"
#include "infrastructure/postgres/pg_utils.hpp"

namespace inerxia::infrastructure::postgres {

domain::SubscriberId PostgresSubscriberRepository::next_id() {
    return domain::SubscriberId{next_uuid_v4()};
}

std::optional<domain::Subscriber> PostgresSubscriberRepository::find_by_id(
    const domain::SubscriberId& id) const {
    auto connection = pool_.acquire();
    connection->prepare("subscriber_select_by_id",
                       "SELECT id, name, static_ip FROM subscribers WHERE id = $1");
    const PgResult result =
        connection->exec_prepared("subscriber_select_by_id", {std::string{id.value()}});
    if (result.row_count() == 0) {
        return std::nullopt;
    }
    const PgRow row{result, 0};
    return domain::Subscriber{domain::SubscriberId{row.required_text(0)},
                              row.required_text(1), domain::IPAddress{row.required_text(2)}};
}

std::vector<domain::Subscriber> PostgresSubscriberRepository::find_all() const {
    auto connection = pool_.acquire();
    connection->prepare("subscriber_select_all",
                        "SELECT id, name, static_ip FROM subscribers ORDER BY name");
    const PgResult result = connection->exec_prepared("subscriber_select_all", {});
    std::vector<domain::Subscriber> subscribers;
    subscribers.reserve(static_cast<std::size_t>(result.row_count()));
    for (int row = 0; row < result.row_count(); ++row) {
        const PgRow r{result, row};
        subscribers.emplace_back(domain::SubscriberId{r.required_text(0)}, r.required_text(1),
                                 domain::IPAddress{r.required_text(2)});
    }
    return subscribers;
}

void PostgresSubscriberRepository::save(const domain::Subscriber& subscriber) {
    auto connection = pool_.acquire();
    connection->prepare(
        "subscriber_upsert",
        "INSERT INTO subscribers (id, name, static_ip) VALUES ($1, $2, $3) "
        "ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name, static_ip = EXCLUDED.static_ip");
    connection->exec_prepared("subscriber_upsert",
                              {std::string{subscriber.id().value()}, subscriber.name(),
                               subscriber.static_ip().value()});
}

}  // namespace inerxia::infrastructure::postgres