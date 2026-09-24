// API tests: exercise the full HTTP surface end-to-end.
//
//   HTTP request -> Controller -> Use case -> Domain -> repository / router
//
// Controllers contain no business logic by construction; these tests verify the
// HTTP contract (status codes + JSON payloads), the wiring of the composition
// root, and that domain rules surface as the documented errors.
//
// Env-gating:
//   * PostgreSQL is REQUIRED (libpq + live server). Skip when PGUSER/PGPASSWORD
//     are unset or the server is unreachable (same policy as the repository
//     integration tests; the suite uses the dedicated inerxia_test database).
//   * The live MikroTik adapter is used when MIKROTIK_BASE_URL/USER/PASSWORD are
//     set; otherwise a failing adapter stands in for the router so the tests
//     that do NOT need a working router still run (they assert 503 instead).
//
// Run with:
//   export PGUSER=inerxia PGPASSWORD=...
//   export MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest MIKROTIK_USER=... MIKROTIK_PASSWORD=...
//   ctest --test-dir build -L api

#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "api/api_server.hpp"
#include "api/app_services.hpp"
#include "application/ports/RouterGateway.h"
#include "application/ports/time_provider.hpp"
#include "infrastructure/infrastructure_error.hpp"
#include "infrastructure/mikrotik/router_gateway_factory.hpp"
#include "infrastructure/postgres/migrations.hpp"
#include "infrastructure/postgres/pg_connection.hpp"
#include "infrastructure/postgres/pg_utils.hpp"
#include "infrastructure/postgres/postgres_config.hpp"

namespace {

using namespace std::chrono;

using inerxia::api::ApiServer;
using inerxia::api::AppServices;
using inerxia::application::RouterGateway;
using inerxia::infrastructure::RouterOSApiError;

// Fixed "today" so status derivations are deterministic across runs.
constexpr auto kToday = year{2026}/10/2;

std::optional<std::string> env(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return std::string{value};
    }
    return std::nullopt;
}

std::string unique_static_ip() {
    const std::string uuid = inerxia::infrastructure::postgres::next_uuid_v4();
    return "10." + std::to_string((uuid[0] % 200) + 1) + "." +
           std::to_string((uuid[1] % 200) + 1) + "." + std::to_string((uuid[2] % 200) + 1);
}

class FakeTimeProvider final : public inerxia::application::TimeProvider {
public:
    std::chrono::year_month_day today() const override { return kToday; }
};

// Stand-in for the router used only when MIKROTIK_* is not configured: every
// call fails, so router-dependent endpoints surface as 503 and the rest of the
// surface can still be tested. This fake never appears in production code.
class FailingRouterGateway final : public RouterGateway {
public:
    void enableUser(const inerxia::domain::ContractId&, const inerxia::domain::IPAddress&) override {
        throw RouterOSApiError("MikroTik not configured");
    }
    void disableUser(const inerxia::domain::ContractId&, const inerxia::domain::IPAddress&) override {
        throw RouterOSApiError("MikroTik not configured");
    }
    void changeSpeedProfile(const inerxia::domain::ContractId&, const inerxia::domain::IPAddress&,
                            const inerxia::domain::SpeedProfile&) override {
        throw RouterOSApiError("MikroTik not configured");
    }
};

struct TestDatabase {
    std::unique_ptr<inerxia::infrastructure::postgres::PostgresPool> pool;
    std::string unavailability = "PostgreSQL not configured";
    bool attempted = false;

    void ensure() {
        if (attempted) {
            return;
        }
        attempted = true;
        if (!env("PGUSER").has_value() || !env("PGPASSWORD").has_value()) {
            unavailability =
                "API tests skipped: PGUSER/PGPASSWORD not set (PostgreSQL required)";
            return;
        }
        inerxia::infrastructure::postgres::PostgresConfig config;
        config.host = env("PGHOST").value_or("127.0.0.1");
        config.port = [&] {
            if (const auto port = env("PGPORT")) {
                return std::stoi(*port);
            }
            return 5432;
        }();
        config.dbname = env("PGDATABASE").value_or("inerxia_test");
        config.user = *env("PGUSER");
        config.password = *env("PGPASSWORD");
        config.sslmode = env("PGSSLMODE").value_or("disable");

        try {
            auto created = std::make_unique<inerxia::infrastructure::postgres::PostgresPool>(config);
            auto connection = created->acquire();
            inerxia::infrastructure::postgres::apply_migrations(*connection);
            connection->exec(
                "TRUNCATE payments, contracts, subscribers, internet_plans, audit_logs, "
                "users CASCADE");
            pool = std::move(created);
        } catch (const std::exception& error) {
            unavailability = "API tests skipped: " + std::string{error.what()};
        }
    }
};

TestDatabase& test_database() {
    static TestDatabase database;
    database.ensure();
    return database;
}

class ApiHttpTest : public ::testing::Test {
protected:
    void SetUp() override {
        TestDatabase& database = test_database();
        if (!database.pool) {
            GTEST_SKIP() << database.unavailability;
        }
        pool_ = database.pool.get();
        clock_ = std::make_unique<FakeTimeProvider>();
        router_ = make_router();
        live_router_ = router_ != nullptr;
        if (!live_router_) {
            router_ = std::make_unique<FailingRouterGateway>();
        }
        services_ = std::make_unique<AppServices>(*pool_, *router_, *clock_);
        server_ = std::make_unique<ApiServer>(*services_, *pool_);
        port_ = server_->bind_to_any_port();
        if (port_ <= 0) {
            GTEST_SKIP() << "Failed to bind an ephemeral API port";
        }
        worker_ = std::make_unique<std::thread>([this] { server_->listen_after_bind(); });
        authenticate();
    }

    // Registers and logs in the per-test operator so client() is authorized.
    // Registration/login are public routes, so no token exists yet.
    void authenticate() {
        httplib::Client http{"127.0.0.1", port_};
        username_ = "tester_" + std::to_string(port_);
        const auto registered =
            http.Post("/api/auth/register",
                      nlohmann::json{{"username", username_}, {"password", "s3cret-pass"}}.dump(),
                      "application/json");
        ASSERT_EQ(registered->status, 201);
        const auto login = http.Post(
            "/api/auth/login",
            nlohmann::json{{"username", username_}, {"password", "s3cret-pass"}}.dump(),
            "application/json");
        ASSERT_EQ(login->status, 200);
        token_ = body(login).at("token").get<std::string>();
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        if (worker_ && worker_->joinable()) {
            worker_->join();
        }
    }

    httplib::Client client() const {
        httplib::Client http{"127.0.0.1", port_};
        if (!token_.empty()) {
            http.set_default_headers(
                httplib::Headers{{"Authorization", "Bearer " + token_}});
        }
        return http;
    }

    // A client with no credentials, for the tests that exercise the auth gate.
    httplib::Client anonymous_client() const { return httplib::Client{"127.0.0.1", port_}; }

    bool live_router() const noexcept { return live_router_; }

    AppServices& services() const { return *services_; }

    const std::string& username() const noexcept { return username_; }

    static nlohmann::json body(const httplib::Result& result) {
        if (!result) {
            return nlohmann::json{};
        }
        return nlohmann::json::parse(result->body);
    }

    // Fluent helpers: create a subscriber/plan/contract through the API and
    // return the created id.
    std::string create_subscriber(httplib::Client& http, const std::string& ip) {
        const auto result = http.Post(
            "/api/subscribers", nlohmann::json{{"name", "Ana"}, {"static_ip", ip}}.dump(),
            "application/json");
        EXPECT_EQ(result->status, 201);
        return body(result).at("id").get<std::string>();
    }

    std::string create_plan(httplib::Client& http) {
        const auto result = http.Post(
            "/api/plans", nlohmann::json{{"name", "Fibra 300"},
                                          {"download_mbps", 300},
                                          {"upload_mbps", 150},
                                          {"monthly_price_cents", 15000}}
                              .dump(),
            "application/json");
        EXPECT_EQ(result->status, 201);
        return body(result).at("id").get<std::string>();
    }

    std::string create_contract(httplib::Client& http, const std::string& subscriber,
                                const std::string& plan, const std::string& due_date = "2026-12-01") {
        const auto result =
            http.Post("/api/contracts",
                      nlohmann::json{{"subscriber_id", subscriber},
                                     {"plan_id", plan},
                                     {"billing_start", "2026-09-01"},
                                     {"due_date", due_date}}
                          .dump(),
                      "application/json");
        EXPECT_EQ(result->status, 201);
        return body(result).at("id").get<std::string>();
    }

private:
    static std::unique_ptr<RouterGateway> make_router() {
        try {
            if (env("MIKROTIK_BASE_URL").has_value() && env("MIKROTIK_USER").has_value() &&
                env("MIKROTIK_PASSWORD").has_value()) {
                return inerxia::infrastructure::make_mikrotik_router_gateway_from_env();
            }
        } catch (const std::exception& error) {
            (void)error;
        }
        return nullptr;
    }

    inerxia::infrastructure::postgres::PostgresPool* pool_ = nullptr;
    std::unique_ptr<FakeTimeProvider> clock_;
    std::unique_ptr<RouterGateway> router_;
    std::unique_ptr<AppServices> services_;
    std::unique_ptr<ApiServer> server_;
    std::unique_ptr<std::thread> worker_;
    int port_ = -1;
    bool live_router_ = false;
    std::string username_;
    std::string token_;
};

TEST_F(ApiHttpTest, CrudLifecycleOverHttp) {
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    const std::string contract_id =
        create_contract(http, subscriber_id, plan_id, /*due_date=*/"2026-12-01");

    const auto created = body(http.Get("/api/contracts/" + contract_id));
    EXPECT_EQ(created.at("status"), "active");
    EXPECT_EQ(created.at("suspended"), false);
    EXPECT_EQ(created.at("subscriber_id"), subscriber_id);
    EXPECT_EQ(created.at("plan_id"), plan_id);
    EXPECT_EQ(created.at("download_mbps"), 300);
    EXPECT_EQ(created.at("billing_start"), "2026-09-01");
    EXPECT_EQ(created.at("due_date"), "2026-12-01");
    EXPECT_TRUE(created.at("payments").empty());

    const auto rescheduled = http.Put("/api/contracts/" + contract_id,
                                      nlohmann::json{{"due_date", "2027-01-01"}}.dump(),
                                      "application/json");
    EXPECT_EQ(rescheduled->status, 200);
    EXPECT_EQ(body(rescheduled).at("due_date"), "2027-01-01");

    const auto payment = http.Post(
        "/api/contracts/" + contract_id + "/payments",
        nlohmann::json{{"amount_cents", 15000}, {"paid_on", "2026-09-15"}}.dump(),
        "application/json");
    ASSERT_EQ(payment->status, 201);
    EXPECT_EQ(body(payment).at("contract_id"), contract_id);
    EXPECT_EQ(body(payment).at("amount_cents"), 15000);

    const auto after_payment = body(http.Get("/api/contracts/" + contract_id));
    ASSERT_EQ(after_payment.at("payments").size(), 1U);
    EXPECT_EQ(after_payment.at("payments")[0].at("amount_cents"), 15000);
}

TEST_F(ApiHttpTest, GetUnknownContractReturns404) {
    auto http = client();
    const auto response = http.Get("/api/contracts/does-not-exist");
    ASSERT_EQ(response->status, 404);
    EXPECT_EQ(body(response).at("error"), "not_found");
}

TEST_F(ApiHttpTest, CreateContractWithUnknownSubscriberReturns404) {
    auto http = client();
    const auto response = http.Post(
        "/api/contracts", nlohmann::json{{"subscriber_id", "nope"},
                                     {"plan_id", "nope"},
                                     {"billing_start", "2026-09-01"},
                                     {"due_date", "2026-12-01"}}
                           .dump(),
        "application/json");
    ASSERT_EQ(response->status, 404);
    EXPECT_EQ(body(response).at("error"), "not_found");
}

TEST_F(ApiHttpTest, CreateContractWithMalformedDateReturns400) {
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    const auto response = http.Post(
        "/api/contracts", nlohmann::json{{"subscriber_id", subscriber_id},
                                     {"plan_id", plan_id},
                                     {"billing_start", "2026-13-40"},
                                     {"due_date", "2026-12-01"}}
                           .dump(),
        "application/json");
    ASSERT_EQ(response->status, 400);
    EXPECT_EQ(body(response).at("error"), "bad_request");
}

TEST_F(ApiHttpTest, CreateContractWithReversedDatesReturns422) {
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    const auto response = http.Post(
        "/api/contracts", nlohmann::json{{"subscriber_id", subscriber_id},
                                     {"plan_id", plan_id},
                                     {"billing_start", "2026-12-01"},
                                     {"due_date", "2026-09-01"}}
                           .dump(),
        "application/json");
    ASSERT_EQ(response->status, 422);
    EXPECT_EQ(body(response).at("error"), "domain_rule");
}

TEST_F(ApiHttpTest, SuspendAndReactivateThroughHttp) {
    if (!live_router()) {
        GTEST_SKIP() << "Live RouterOS not configured; router endpoints return 503";
    }
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    const std::string contract_id = create_contract(http, subscriber_id, plan_id);

    const auto suspended = http.Post("/api/contracts/" + contract_id + "/suspend");
    ASSERT_EQ(suspended->status, 200);
    EXPECT_EQ(body(suspended).at("suspended"), true);
    EXPECT_EQ(body(suspended).at("status"), "suspended");

    const auto reactivated = http.Post("/api/contracts/" + contract_id + "/reactivate");
    ASSERT_EQ(reactivated->status, 200);
    EXPECT_EQ(body(reactivated).at("suspended"), false);
    EXPECT_EQ(body(reactivated).at("status"), "active");
}

TEST_F(ApiHttpTest, ChangeSpeedProfileThroughHttp) {
    if (!live_router()) {
        GTEST_SKIP() << "Live RouterOS not configured; router endpoints return 503";
    }
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    const std::string contract_id = create_contract(http, subscriber_id, plan_id);

    const auto response =
        http.Patch("/api/contracts/" + contract_id + "/speed-profile",
                   nlohmann::json{{"download_mbps", 600}, {"upload_mbps", 300}}.dump(),
                   "application/json");
    ASSERT_EQ(response->status, 200);
    const auto contract = body(response);
    EXPECT_EQ(contract.at("download_mbps"), 600);
    EXPECT_EQ(contract.at("upload_mbps"), 300);
}

TEST_F(ApiHttpTest, RouterOpsReturn503WhenRouterUnavailable) {
    if (live_router()) {
        GTEST_SKIP() << "Live RouterOS configured; failing-router behavior not testable";
    }
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    const std::string contract_id = create_contract(http, subscriber_id, plan_id);

    const auto suspended = http.Post("/api/contracts/" + contract_id + "/suspend");
    ASSERT_EQ(suspended->status, 503);
    EXPECT_EQ(body(suspended).at("error"), "infrastructure");
}

TEST_F(ApiHttpTest, SweepSuspendsOverdueAndPaymentReactivates) {
    if (!live_router()) {
        GTEST_SKIP() << "Live RouterOS not configured; router endpoints return 503";
    }
    auto http = client();
    const std::string subscriber_id = create_subscriber(http, unique_static_ip());
    const std::string plan_id = create_plan(http);
    // Due 2026-10-01 while "today" is 2026-10-02 -> overdue, sweepable.
    const std::string contract_id =
        create_contract(http, subscriber_id, plan_id, /*due_date=*/"2026-10-01");

    const auto swept = http.Post("/api/sweep/expired");
    ASSERT_EQ(swept->status, 200);
    const auto ids = body(swept).at("suspended_contract_ids");
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0].get<std::string>(), contract_id);

    const auto after_sweep = body(http.Get("/api/contracts/" + contract_id));
    EXPECT_EQ(after_sweep.at("status"), "suspended");

    const auto payment = http.Post(
        "/api/contracts/" + contract_id + "/payments",
        nlohmann::json{{"amount_cents", 15000}, {"paid_on", "2026-10-02"}}.dump(),
        "application/json");
    ASSERT_EQ(payment->status, 201);

    const auto reactivated = body(http.Get("/api/contracts/" + contract_id));
    EXPECT_EQ(reactivated.at("status"), "active");
    ASSERT_EQ(reactivated.at("payments").size(), 1U);
}

TEST_F(ApiHttpTest, HealthReportsOkWhenDatabaseIsUp) {
    auto http = client();
    const auto response = http.Get("/api/health");
    ASSERT_EQ(response->status, 200);
    EXPECT_EQ(body(response).at("status"), "ok");
}

TEST_F(ApiHttpTest, HealthReports503WhenDatabaseUnavailable) {
    // A pool whose connections all fail (nothing listens on port 1): the health
    // probe must answer 503 instead of reporting ok.
    inerxia::infrastructure::postgres::PostgresConfig config;
    config.host = "127.0.0.1";
    config.port = 1;
    config.dbname = "inerxia_test";
    config.user = *env("PGUSER");
    config.password = *env("PGPASSWORD");
    inerxia::infrastructure::postgres::PostgresPool dead_pool{config};

    ApiServer degraded_server{services(), dead_pool};
    const int degraded_port = degraded_server.bind_to_any_port();
    ASSERT_GT(degraded_port, 0);
    std::thread degraded_worker{[&] { degraded_server.listen_after_bind(); }};

    httplib::Client http{"127.0.0.1", degraded_port};
    const auto response = http.Get("/api/health");
    degraded_server.stop();
    degraded_worker.join();

    ASSERT_EQ(response->status, 503);
    EXPECT_EQ(body(response).at("error"), "infrastructure");
}

TEST_F(ApiHttpTest, OpenApiSpecIsServed) {
    auto http = client();
    const auto response = http.Get("/api/openapi.json");
    ASSERT_EQ(response->status, 200);
    EXPECT_NE(response->get_header_value("Content-Type").find("application/json"),
              std::string::npos);

    const nlohmann::json spec = nlohmann::json::parse(response->body);
    EXPECT_EQ(spec.at("openapi").get<std::string>().rfind("3.", 0), 0U);
    const auto& paths = spec.at("paths");
    // Every endpoint exercised by the lifecycle tests must be documented.
    EXPECT_TRUE(paths.contains("/api/contracts"));
    EXPECT_TRUE(paths.contains("/api/contracts/{id}"));
    EXPECT_TRUE(paths.contains("/api/contracts/{id}/payments"));
    EXPECT_TRUE(paths.contains("/api/contracts/{id}/suspend"));
    EXPECT_TRUE(paths.contains("/api/contracts/{id}/reactivate"));
    EXPECT_TRUE(paths.contains("/api/contracts/{id}/speed-profile"));
    EXPECT_TRUE(paths.contains("/api/health"));
}

TEST_F(ApiHttpTest, SwaggerUiPageIsServed) {
    auto http = client();
    const auto response = http.Get("/swagger");
    ASSERT_EQ(response->status, 200);
    EXPECT_NE(response->get_header_value("Content-Type").find("text/html"),
              std::string::npos);
    EXPECT_NE(response->body.find("swagger-ui"), std::string::npos);
    EXPECT_NE(response->body.find("/api/openapi.json"), std::string::npos);
}

TEST_F(ApiHttpTest, UiDashboardIsServed) {
    auto http = client();
    const auto response = http.Get("/ui");
    ASSERT_EQ(response->status, 200);
    EXPECT_NE(response->get_header_value("Content-Type").find("text/html"),
              std::string::npos);
    // The dashboard must offer a control for every life-cycle operation.
    EXPECT_NE(response->body.find("Crear cliente"), std::string::npos);
    EXPECT_NE(response->body.find("id del contrato"), std::string::npos);
    EXPECT_NE(response->body.find("Suspender"), std::string::npos);
    EXPECT_NE(response->body.find("Reactivar"), std::string::npos);
    EXPECT_NE(response->body.find("Registrar pago"), std::string::npos);
    EXPECT_NE(response->body.find("/api/contracts/"), std::string::npos);
    // Operator session, subscriber inventory and the change audit are all present.
    EXPECT_NE(response->body.find("Inventario"), std::string::npos);
    EXPECT_NE(response->body.find("Auditoría"), std::string::npos);
    EXPECT_NE(response->body.find("runLogin"), std::string::npos);
    EXPECT_NE(response->body.find("runRefreshAudit"), std::string::npos);
    EXPECT_NE(response->body.find("Authorization"), std::string::npos);
}

TEST_F(ApiHttpTest, RootRedirectsToDashboard) {
    auto http = client();
    const auto response = http.Get("/");
    ASSERT_EQ(response->status, 302);
    EXPECT_EQ(response->get_header_value("Location"), "/ui");
}

// ---------------------------------------------------------------------------
// Authentication (DB-backed users) + change audit + subscriber inventory
// ---------------------------------------------------------------------------

// A self-contained server stack for the authentication tests.
struct AuthApiStack {
    std::unique_ptr<FakeTimeProvider> clock{std::make_unique<FakeTimeProvider>()};
    std::unique_ptr<RouterGateway> router{std::make_unique<FailingRouterGateway>()};
    std::unique_ptr<AppServices> services;
    std::unique_ptr<ApiServer> server;
    std::unique_ptr<std::thread> worker;
    int port = -1;

    void start(inerxia::infrastructure::postgres::PostgresPool& pool) {
        services = std::make_unique<AppServices>(pool, *router, *clock);
        server = std::make_unique<ApiServer>(*services, pool);
        port = server->bind_to_any_port();
        ASSERT_GT(port, 0);
        worker = std::make_unique<std::thread>([this] { server->listen_after_bind(); });
    }

    void stop() {
        if (server) {
            server->stop();
        }
        if (worker && worker->joinable()) {
            worker->join();
        }
    }

    httplib::Client client() const { return httplib::Client{"127.0.0.1", port}; }
};

class AuthApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        TestDatabase& database = test_database();
        if (!database.pool) {
            GTEST_SKIP() << database.unavailability;
        }
        api_.start(*database.pool);
    }

    void TearDown() override { api_.stop(); }

    httplib::Client client() const { return api_.client(); }

    // A username unique to this test process so re-runs never collide with the
    // users left behind by previously executed AuthApiTest cases.
    std::string unique_operator() const {
        static int counter = 0;
        return "auth" + std::to_string(++counter);
    }

    // Registers a fresh operator and returns its bearer token.
    std::string register_login(httplib::Client& http) {
        username_ = unique_operator();
        const auto registered =
            http.Post("/api/auth/register",
                      nlohmann::json{{"username", username_}, {"password", "s3cret-pass"}}.dump(),
                      "application/json");
        EXPECT_EQ(registered->status, 201);
        const auto login = http.Post(
            "/api/auth/login",
            nlohmann::json{{"username", username_}, {"password", "s3cret-pass"}}.dump(),
            "application/json");
        EXPECT_EQ(login->status, 200);
        return body(login).at("token").get<std::string>();
    }

    httplib::Headers bearer(const std::string& token) const {
        return httplib::Headers{{"Authorization", "Bearer " + token}};
    }

    const std::string& username() const noexcept { return username_; }

    static nlohmann::json body(const httplib::Result& result) {
        if (!result) {
            return nlohmann::json{};
        }
        return nlohmann::json::parse(result->body);
    }

private:
    AuthApiStack api_;
    std::string username_;
};

TEST_F(ApiHttpTest, RegisterAndLoginFlow) {
    auto http = anonymous_client();
    const auto registered =
        http.Post("/api/auth/register",
                  nlohmann::json{{"username", "new_operator"}, {"password", "s3cret-pass"}}.dump(),
                  "application/json");
    ASSERT_EQ(registered->status, 201);
    EXPECT_EQ(body(registered).at("username"), "new_operator");

    // Duplicate registration is rejected, weak passwords too.
    const auto duplicate = http.Post(
        "/api/auth/register",
        nlohmann::json{{"username", "NEW_OPERATOR"}, {"password", "other-pass"}}.dump(),
        "application/json");
    ASSERT_EQ(duplicate->status, 409);
    EXPECT_EQ(body(duplicate).at("error"), "username_taken");

    const auto weak = http.Post(
        "/api/auth/register",
        nlohmann::json{{"username", "another_op"}, {"password", "1234567"}}.dump(),
        "application/json");
    ASSERT_EQ(weak->status, 422);
    EXPECT_EQ(body(weak).at("error"), "registration_rule");

    // Login with the registered credentials yields a working token.
    const auto login = http.Post(
        "/api/auth/login",
        nlohmann::json{{"username", "new_operator"}, {"password", "s3cret-pass"}}.dump(),
        "application/json");
    ASSERT_EQ(login->status, 200);
    const std::string token = body(login).at("token").get<std::string>();
    EXPECT_EQ(token.size(), 64u);

    const auto me = http.Get("/api/auth/me", httplib::Headers{{"Authorization", "Bearer " + token}});
    ASSERT_EQ(me->status, 200);
    EXPECT_EQ(body(me).at("authenticated"), true);
    EXPECT_EQ(body(me).at("username"), "new_operator");

    // Wrong credentials never yield a token.
    const auto wrong = http.Post(
        "/api/auth/login",
        nlohmann::json{{"username", "new_operator"}, {"password", "wrong-pass"}}.dump(),
        "application/json");
    ASSERT_EQ(wrong->status, 401);
}

TEST_F(ApiHttpTest, PrivateRoutesRequireAuthentication) {
    auto http = anonymous_client();

    const auto no_token = http.Get("/api/subscribers");
    ASSERT_EQ(no_token->status, 401);
    EXPECT_EQ(body(no_token).at("error"), "unauthorized");

    const auto no_token_write = http.Post(
        "/api/subscribers",
        nlohmann::json{{"name", "Blocked"}, {"static_ip", unique_static_ip()}}.dump(),
        "application/json");
    ASSERT_EQ(no_token_write->status, 401);

    // Public routes keep working without a token.
    const auto health = http.Get("/api/health");
    ASSERT_EQ(health->status, 200);

    // The same request works with the per-test operator's token.
    const auto authorized = client().Get("/api/subscribers");
    ASSERT_EQ(authorized->status, 200);
    EXPECT_TRUE(body(authorized).is_array());
}

TEST_F(ApiHttpTest, SubscriberInventoryListsAllUsers) {
    auto http = client();
    const std::string ip_a = unique_static_ip();
    const std::string ip_b = unique_static_ip();

    const auto first = http.Post(
        "/api/subscribers", nlohmann::json{{"name", "Ana"}, {"static_ip", ip_a}}.dump(),
        "application/json");
    ASSERT_EQ(first->status, 201);
    const auto second = http.Post(
        "/api/subscribers", nlohmann::json{{"name", "Bruno"}, {"static_ip", ip_b}}.dump(),
        "application/json");
    ASSERT_EQ(second->status, 201);

    const auto inventory = http.Get("/api/subscribers");
    ASSERT_EQ(inventory->status, 200);
    const nlohmann::json users = body(inventory);
    ASSERT_TRUE(users.is_array());
    bool has_ana = false;
    bool has_bruno = false;
    for (const auto& user : users) {
        if (user.at("static_ip") == ip_a && user.at("name") == "Ana") {
            has_ana = true;
        }
        if (user.at("static_ip") == ip_b && user.at("name") == "Bruno") {
            has_bruno = true;
        }
    }
    EXPECT_TRUE(has_ana);
    EXPECT_TRUE(has_bruno);
}

TEST_F(ApiHttpTest, AuditRecordsTheLoggedInActor) {
    auto http = client();
    const auto before = http.Get("/api/audit?limit=200");
    ASSERT_EQ(before->status, 200);
    const std::size_t before_count = body(before).size();

    const auto created = http.Post(
        "/api/subscribers",
        nlohmann::json{{"name", "Audited"}, {"static_ip", unique_static_ip()}}.dump(),
        "application/json");
    ASSERT_EQ(created->status, 201);

    const auto after = client().Get("/api/audit?limit=200");
    ASSERT_EQ(after->status, 200);
    const nlohmann::json entries = body(after);
    ASSERT_GE(entries.size(), before_count + 1);

    const auto it = std::find_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.at("method") == "POST" && entry.at("path") == "/api/subscribers" &&
               entry.at("status") == 201;
    });
    ASSERT_NE(it, entries.end());
    EXPECT_EQ(it->at("actor"), username());
    EXPECT_FALSE(it->at("id").get<std::string>().empty());
    // The registration/login password must never leak into the audit log.
    for (const auto& entry : entries) {
        const std::string detail = entry.at("detail").get<std::string>();
        EXPECT_EQ(detail.find("s3cret-pass"), std::string::npos);
    }
}

TEST_F(ApiHttpTest, AuditIsNewestFirst) {
    auto http = client();
    const std::string ip_a = unique_static_ip();
    const std::string ip_b = unique_static_ip();
    ASSERT_EQ(http.Post("/api/subscribers",
                        nlohmann::json{{"name", "Alfa"}, {"static_ip", ip_a}}.dump(),
                        "application/json")
                  ->status,
              201);
    ASSERT_EQ(http.Post("/api/subscribers",
                        nlohmann::json{{"name", "Beta"}, {"static_ip", ip_b}}.dump(),
                        "application/json")
                  ->status,
              201);

    const auto audit = http.Get("/api/audit?limit=5");
    ASSERT_EQ(audit->status, 200);
    const nlohmann::json entries = body(audit);
    ASSERT_GE(entries.size(), 2u);
    // The most recent change (Beta) must appear before the previous one (Alfa).
    EXPECT_GE(entries[0].at("occurred_at").get<std::string>().size(), 20u);
    EXPECT_TRUE(entries[0].at("occurred_at").get<std::string>().ends_with('Z') ||
                entries[0].at("occurred_at").get<std::string>().size() >= 19);
}

TEST_F(AuthApiTest, LoginProtectsPrivateRoutes) {
    auto http = client();

    const auto wrong = http.Post(
        "/api/auth/login", nlohmann::json{{"username", "ghost"}, {"password", "wrong"}}.dump(),
        "application/json");
    ASSERT_EQ(wrong->status, 401);

    const auto no_token = http.Get("/api/subscribers");
    ASSERT_EQ(no_token->status, 401);
    EXPECT_EQ(body(no_token).at("error"), "unauthorized");

    const auto no_token_write = http.Post(
        "/api/subscribers",
        nlohmann::json{{"name", "Blocked"}, {"static_ip", unique_static_ip()}}.dump(),
        "application/json");
    ASSERT_EQ(no_token_write->status, 401);

    // Public routes keep working without a token.
    const auto health = http.Get("/api/health");
    ASSERT_EQ(health->status, 200);

    const std::string token = register_login(http);
    const auto me = http.Get("/api/auth/me", bearer(token));
    ASSERT_EQ(me->status, 200);
    EXPECT_EQ(body(me).at("authenticated"), true);
    EXPECT_EQ(body(me).at("username"), username());

    const auto inventory = http.Get("/api/subscribers", bearer(token));
    ASSERT_EQ(inventory->status, 200);
    EXPECT_TRUE(body(inventory).is_array());

    // The internal actor header must never leak to the client.
    EXPECT_FALSE(inventory->has_header("X-Inerxia-Actor"));
}

TEST_F(AuthApiTest, LogoutRevokesTheSession) {
    auto http = client();
    const std::string token = register_login(http);

    const auto logout = http.Post("/api/auth/logout", bearer(token));
    ASSERT_EQ(logout->status, 204);

    const auto after_logout = http.Get("/api/subscribers", bearer(token));
    ASSERT_EQ(after_logout->status, 401);
    EXPECT_EQ(body(http.Get("/api/auth/me", bearer(token))).at("error"), "unauthorized");
}

TEST_F(AuthApiTest, RegisteredOperatorChangesAppearInTheAuditLog) {
    auto http = client();
    const std::string token = register_login(http);
    const std::string ip = unique_static_ip();

    const auto created = http.Post(
        "/api/subscribers", bearer(token),
        nlohmann::json{{"name", "Observer"}, {"static_ip", ip}}.dump(), "application/json");
    ASSERT_EQ(created->status, 201);

    const auto audit = http.Get("/api/audit", bearer(token));
    ASSERT_EQ(audit->status, 200);
    const nlohmann::json entries = body(audit);
    const auto it = std::find_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.at("method") == "POST" && entry.at("path") == "/api/subscribers" &&
               entry.at("status") == 201;
    });
    ASSERT_NE(it, entries.end());
    EXPECT_EQ(it->at("actor"), username());
    EXPECT_EQ(it->at("detail"), "{\"name\":\"Observer\",\"static_ip\":\"" + ip + "\"}");
}

}  // namespace