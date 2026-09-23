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