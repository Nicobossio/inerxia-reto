// Unit tests for the subscriber inventory use case.

#include <gtest/gtest.h>

#include <string>

#include "application/use_cases/list_subscribers.hpp"
#include "application_fakes.hpp"
#include "domain/ip_address.hpp"

namespace {

using namespace inerxia::application;
using test::InMemorySubscriberRepository;

TEST(ListSubscribers, EmptyInventoryWhenNoSubscribersExist) {
    InMemorySubscriberRepository repository;
    ListSubscribers list{repository};
    EXPECT_TRUE(list().empty());
}

TEST(ListSubscribers, ListsEverySavedSubscriber) {
    InMemorySubscriberRepository repository;
    repository.save(inerxia::domain::Subscriber{repository.next_id(), "Ana",
                                                inerxia::domain::IPAddress{"10.0.0.1"}});
    repository.save(inerxia::domain::Subscriber{repository.next_id(), "Bruno",
                                                inerxia::domain::IPAddress{"10.0.0.2"}});
    repository.save(inerxia::domain::Subscriber{repository.next_id(), "Carla",
                                                inerxia::domain::IPAddress{"10.0.0.3"}});

    const auto inventory = ListSubscribers{repository}();
    ASSERT_EQ(inventory.size(), 3u);
    EXPECT_EQ(inventory.at(0).static_ip().value(), "10.0.0.1");
    EXPECT_EQ(inventory.at(1).name(), "Bruno");
    EXPECT_EQ(inventory.at(2).id().value().substr(0, 4), "sub-");
}

}  // namespace