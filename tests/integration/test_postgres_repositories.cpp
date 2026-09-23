// Integration tests for the PostgreSQL repositories against a REAL Postgres.
//
// These tests never touch production data:
//   * They connect to a separate database: PGDATABASE if the operator sets it,
//     otherwise the dedicated "inerxia_test" database (never "inerxia").
//   * Each entity gets a fresh random id, so re-runs cannot collide.
//
// Env-gated like the MikroTik tests: skip when PGUSER/PGPASSWORD are unset or the
// server is unreachable, so plain `ctest` stays green without a database.
//
// Run them with:
//   export PGUSER=inerxia PGPASSWORD=...      # PGDATABASE defaults to inerxia_test
//   ctest --test-dir build -L integration

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "domain/contract.hpp"
#include "domain/contract_status.hpp"
#include "domain/internet_plan.hpp"
#include "domain/money.hpp"
#include "domain/payment.hpp"
#include "domain/subscriber.hpp"
#include "infrastructure/postgres/migrations.hpp"
#include "infrastructure/postgres/pg_connection.hpp"
#include "infrastructure/postgres/pg_row.hpp"
#include "infrastructure/postgres/pg_utils.hpp"
#include "infrastructure/postgres/postgres_config.hpp"
#include "infrastructure/postgres/postgres_contract_repository.hpp"
#include "infrastructure/postgres/postgres_internet_plan_repository.hpp"
#include "infrastructure/postgres/postgres_payment_repository.hpp"
#include "infrastructure/postgres/postgres_subscriber_repository.hpp"

namespace {

using namespace inerxia::domain;
using namespace inerxia::infrastructure::postgres;

using inerxia::application::ContractRepository;
using inerxia::application::InternetPlanRepository;
using inerxia::application::SubscriberRepository;
using inerxia::infrastructure::PostgresError;

using namespace std::chrono;

constexpr auto kBillingStart = year{2026}/9/1;
constexpr auto kDueDate = year{2026}/10/1;
constexpr auto kPrice = Money::from_cents(15000);

std::optional<std::string> env(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return std::string{value};
    }
    return std::nullopt;
}

std::string unique_static_ip() {
    const std::string uuid = next_uuid_v4();
    const int third = (uuid[0] % 200) + 1;
    const int fourth = (uuid[1] % 200) + 1;
    const int fifth = (uuid[2] % 200) + 1;
    return "10." + std::to_string(third) + "." + std::to_string(fourth) + "." +
           std::to_string(fifth);
}

struct TestDatabase {
    std::unique_ptr<PostgresPool> pool;
    std::string unavailability = "PostgreSQL not configured";
    bool attempted = false;

    void ensure() {
        if (attempted) {
            return;
        }
        attempted = true;
        if (!env("PGUSER").has_value() || !env("PGPASSWORD").has_value()) {
            unavailability =
                "PostgreSQL integration tests skipped: PGUSER/PGPASSWORD not set";
            return;
        }
        PostgresConfig config;
        config.host = env("PGHOST").value_or("127.0.0.1");
        config.port = [] {
            if (const auto port = env("PGPORT")) {
                return std::stoi(*port);
            }
            return 5432;
        }();
        // Deliberately a dedicated test database, never the app default.
        config.dbname = env("PGDATABASE").value_or("inerxia_test");
        config.user = *env("PGUSER");
        config.password = *env("PGPASSWORD");
        config.sslmode = env("PGSSLMODE").value_or("disable");

        try {
            auto created = std::make_unique<PostgresPool>(config);
            auto connection = created->acquire();
            apply_migrations(*connection);
            // The suite owns this database: start from a clean slate so re-runs
            // against the persistent test database never collide with leftovers.
            connection->exec("TRUNCATE payments, contracts, subscribers, internet_plans CASCADE");
            pool = std::move(created);
        } catch (const std::exception& error) {
            unavailability =
                "PostgreSQL integration tests skipped: " + std::string{error.what()};
        }
    }
};

TestDatabase& test_database() {
    static TestDatabase database;
    database.ensure();
    return database;
}

class PostgresRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        TestDatabase& database = test_database();
        if (!database.pool) {
            GTEST_SKIP() << database.unavailability;
        }
        pool_ = database.pool.get();
    }

    PostgresPool& pool() { return *pool_; }

    SubscriberId new_subscriber(SubscriberRepository& repository) {
        const SubscriberId id{next_uuid_v4()};
        repository.save(Subscriber{id, "Subscriber " + std::string{id.value()},
                                   IPAddress{unique_static_ip()}});
        return id;
    }

    PlanId new_plan(InternetPlanRepository& repository) {
        const PlanId id{next_uuid_v4()};
        repository.save(InternetPlan{id, "Plan " + std::string{id.value()},
                                     SpeedProfile{300, 150}, Money::from_cents(12000)});
        return id;
    }

    ContractId new_contract(ContractRepository& contracts, const SubscriberId& subscriber,
                            const PlanId& plan) {
        const ContractId id{contracts.next_id()};
        contracts.save(Contract{id, subscriber, plan, (SpeedProfile{300, 150}),
                                kBillingStart, kDueDate, Money::from_cents(15000)});
        return id;
    }

private:
    PostgresPool* pool_ = nullptr;
};

TEST_F(PostgresRepositoryTest, MigrationsApplyAndAreIdempotent) {
    auto connection = pool().acquire();
    EXPECT_NO_THROW(apply_migrations(*connection));
    EXPECT_NO_THROW(apply_migrations(*connection));
    const PgResult versions =
        connection->exec("SELECT version, name FROM schema_migrations ORDER BY version");
    ASSERT_GE(versions.row_count(), 2);
    const PgRow first{versions, 0};
    EXPECT_EQ(first.integer(0), 1);
    EXPECT_EQ(first.required_text(1), "create_tables");
    const PgRow second{versions, 1};
    EXPECT_EQ(second.integer(0), 2);
    EXPECT_EQ(second.required_text(1), "referential_integrity_and_query_indexes");
}

TEST_F(PostgresRepositoryTest, SubscriberSaveAndFindRoundTrip) {
    PostgresSubscriberRepository repository{pool()};
    const SubscriberId id{next_uuid_v4()};
    const auto ip = unique_static_ip();
    repository.save(Subscriber{id, "Juana Perez", IPAddress{ip}});

    const auto loaded = repository.find_by_id(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->id(), id);
    EXPECT_EQ(loaded->name(), "Juana Perez");
    EXPECT_EQ(loaded->static_ip(), IPAddress{ip});
    EXPECT_FALSE(repository.find_by_id(SubscriberId{next_uuid_v4()}).has_value());
}

TEST_F(PostgresRepositoryTest, SubscriberSaveUpdatesExistingRow) {
    PostgresSubscriberRepository repository{pool()};
    const SubscriberId id{next_uuid_v4()};
    const auto ip = unique_static_ip();
    repository.save(Subscriber{id, "Old name", IPAddress{ip}});
    repository.save(Subscriber{id, "New name", IPAddress{ip}});

    const auto loaded = repository.find_by_id(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->name(), "New name");
}

TEST_F(PostgresRepositoryTest, SubscriberStaticIpIsUnique) {
    PostgresSubscriberRepository repository{pool()};
    const auto ip = unique_static_ip();
    repository.save(Subscriber{SubscriberId{next_uuid_v4()}, "First", IPAddress{ip}});
    EXPECT_THROW(
        repository.save(Subscriber{SubscriberId{next_uuid_v4()}, "Second", IPAddress{ip}}),
        PostgresError);
}

TEST_F(PostgresRepositoryTest, InternetPlanSaveAndFindRoundTrip) {
    PostgresInternetPlanRepository repository{pool()};
    const PlanId id{next_uuid_v4()};
    repository.save(InternetPlan{id, "Fibra 300", (SpeedProfile{300, 150}),
                                 Money::from_cents(12000)});

    const auto loaded = repository.find_by_id(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->id(), id);
    EXPECT_EQ(loaded->name(), "Fibra 300");
    EXPECT_EQ(loaded->speed(), (SpeedProfile{300, 150}));
    EXPECT_EQ(loaded->monthly_price(), Money::from_cents(12000));
    EXPECT_FALSE(repository.find_by_id(PlanId{next_uuid_v4()}).has_value());
}

TEST_F(PostgresRepositoryTest, ContractRoundTripIncludesPayments) {
    PostgresSubscriberRepository subscribers{pool()};
    PostgresInternetPlanRepository plans{pool()};
    PostgresContractRepository contracts{pool()};
    const auto subscriber = new_subscriber(subscribers);
    const auto plan = new_plan(plans);
    const auto contract_id = new_contract(contracts, subscriber, plan);

    auto contract = contracts.find_by_id(contract_id);
    ASSERT_TRUE(contract.has_value());
    auto& entity = *contract;
    entity.attach_payment(Payment{PaymentId{next_uuid_v4()}, contract_id,
                                  Money::from_cents(10000), year{2026}/9/15});
    contracts.save(entity);

    const auto loaded = contracts.find_by_id(contract_id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->subscriber_id(), subscriber);
    EXPECT_EQ(loaded->plan_id(), plan);
    EXPECT_EQ(loaded->speed_profile(), (SpeedProfile{300, 150}));
    EXPECT_EQ(loaded->billing_start(), kBillingStart);
    EXPECT_EQ(loaded->due_date(), kDueDate);
    EXPECT_EQ(loaded->price_per_period(), Money::from_cents(15000));
    EXPECT_FALSE(loaded->is_suspended());
    EXPECT_EQ(loaded->payments().size(), 1U);
    EXPECT_EQ(loaded->balance_as_of(year{2026}/9/30), Money::from_cents(5000));
}

TEST_F(PostgresRepositoryTest, ContractSavePersistsSuspensionAndReactivation) {
    PostgresSubscriberRepository subscribers{pool()};
    PostgresInternetPlanRepository plans{pool()};
    PostgresContractRepository contracts{pool()};
    const auto subscriber = new_subscriber(subscribers);
    const auto plan = new_plan(plans);
    const auto contract_id = new_contract(contracts, subscriber, plan);

    auto contract = contracts.find_by_id(contract_id);
    ASSERT_TRUE(contract.has_value());
    contract->suspend(SuspensionReason::Manual);
    contracts.save(*contract);

    auto suspended = contracts.find_by_id(contract_id);
    ASSERT_TRUE(suspended.has_value());
    EXPECT_TRUE(suspended->is_suspended());
    EXPECT_EQ(suspended->suspended_reason(), SuspensionReason::Manual);
    EXPECT_EQ(suspended->status_as_of(kBillingStart), ContractStatus::Suspended);

    suspended->reactivate();
    contracts.save(*suspended);
    const auto active = contracts.find_by_id(contract_id);
    ASSERT_TRUE(active.has_value());
    EXPECT_FALSE(active->is_suspended());
    EXPECT_EQ(active->status_as_of(kBillingStart), ContractStatus::Active);
}

TEST_F(PostgresRepositoryTest, ContractSaveReplacesItsPayments) {
    PostgresSubscriberRepository subscribers{pool()};
    PostgresInternetPlanRepository plans{pool()};
    PostgresContractRepository contracts{pool()};
    const auto subscriber = new_subscriber(subscribers);
    const auto plan = new_plan(plans);
    const auto contract_id = new_contract(contracts, subscriber, plan);

    auto contract = contracts.find_by_id(contract_id);
    ASSERT_TRUE(contract.has_value());
    contract->attach_payment(Payment{PaymentId{next_uuid_v4()}, contract_id,
                                     Money::from_cents(10000), year{2026}/9/15});
    contracts.save(*contract);

    contract = contracts.find_by_id(contract_id);
    ASSERT_TRUE(contract.has_value());
    contract->attach_payment(Payment{PaymentId{next_uuid_v4()}, contract_id,
                                     Money::from_cents(5000), year{2026}/9/20});
    contracts.save(*contract);

    const auto loaded = contracts.find_by_id(contract_id);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->payments().size(), 2U);
    EXPECT_EQ(loaded->balance_as_of(year{2026}/9/30), Money::from_cents(0));
}

TEST_F(PostgresRepositoryTest, PaymentRepositorySaveIsIdempotent) {
    PostgresSubscriberRepository subscribers{pool()};
    PostgresInternetPlanRepository plans{pool()};
    PostgresContractRepository contracts{pool()};
    PostgresPaymentRepository payments{pool()};
    const auto subscriber = new_subscriber(subscribers);
    const auto plan = new_plan(plans);
    const auto contract_id = new_contract(contracts, subscriber, plan);

    // Mirrors RegisterPayment: contract first persists the aggregate (with the
    // payment), then PaymentRepository::save upserts the same row.
    const PaymentId payment_id{next_uuid_v4()};
    const Payment payment{payment_id, contract_id, Money::from_cents(15000), year{2026}/10/1};
    auto contract = contracts.find_by_id(contract_id);
    ASSERT_TRUE(contract.has_value());
    contract->attach_payment(payment);
    contracts.save(*contract);
    EXPECT_NO_THROW(payments.save(payment));

    const auto loaded = contracts.find_by_id(contract_id);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->payments().size(), 1U);
    EXPECT_EQ(loaded->payments().front().id(), payment_id);
    EXPECT_EQ(loaded->balance_as_of(year{2026}/10/1), Money::from_cents(0));
}

TEST_F(PostgresRepositoryTest, ContractAllLoadsAggregatesWithPayments) {
    PostgresSubscriberRepository subscribers{pool()};
    PostgresInternetPlanRepository plans{pool()};
    PostgresContractRepository contracts{pool()};
    const auto subscriber = new_subscriber(subscribers);
    const auto plan = new_plan(plans);
    const auto first_id = new_contract(contracts, subscriber, plan);
    const auto second_id = new_contract(contracts, subscriber, plan);

    auto first = contracts.find_by_id(first_id);
    ASSERT_TRUE(first.has_value());
    first->attach_payment(Payment{PaymentId{next_uuid_v4()}, first_id,
                                  Money::from_cents(15000), year{2026}/9/20});
    contracts.save(*first);

    const auto all = contracts.all();
    const auto it_first = std::find_if(all.begin(), all.end(), [&](const auto& c) {
        return c.id() == first_id;
    });
    const auto it_second = std::find_if(all.begin(), all.end(), [&](const auto& c) {
        return c.id() == second_id;
    });
    ASSERT_NE(it_first, all.end());
    ASSERT_NE(it_second, all.end());
    EXPECT_EQ(it_first->payments().size(), 1U);
    EXPECT_EQ(it_first->balance_as_of(kDueDate), Money::from_cents(0));
    EXPECT_TRUE(it_second->payments().empty());
    EXPECT_EQ(it_second->balance_as_of(kDueDate), Money::from_cents(15000));
}

TEST_F(PostgresRepositoryTest, ContractSaveEnforcesForeignKeys) {
    PostgresSubscriberRepository subscribers{pool()};
    PostgresInternetPlanRepository plans{pool()};
    PostgresContractRepository contracts{pool()};
    const auto plan = new_plan(plans);

    const ContractId id{contracts.next_id()};
    const Contract orphan{id, SubscriberId{next_uuid_v4()}, plan, (SpeedProfile{300, 150}),
                          kBillingStart, kDueDate, kPrice};
    EXPECT_THROW(contracts.save(orphan), PostgresError);
    EXPECT_FALSE(contracts.find_by_id(id).has_value());
}

TEST_F(PostgresRepositoryTest, NextIdProducesDistinctIds) {
    PostgresContractRepository contracts{pool()};
    PostgresPaymentRepository payments{pool()};
    const auto a = contracts.next_id();
    const auto b = contracts.next_id();
    const auto c = payments.next_id();
    const auto d = payments.next_id();
    EXPECT_NE(a, b);
    EXPECT_NE(c, d);
    EXPECT_FALSE(std::string{a.value()}.empty());
    EXPECT_FALSE(std::string{c.value()}.empty());
}

}  // namespace