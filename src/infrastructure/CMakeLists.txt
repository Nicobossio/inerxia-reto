# Infrastructure layer: PostgreSQL, HTTP, MikroTik adapters.
# Only this layer may depend on HTTP, JSON, libcurl or RouterOS specifics.

add_library(inerxia_infrastructure STATIC
    auth/in_memory_session_store.cpp
    auth/openssl_pbkdf2_hasher.cpp
    mikrotik/MikrotikEnvConfig.cpp
    mikrotik/MikrotikRouterGateway.cpp
    mikrotik/retrying_router_gateway.cpp
    scheduler/periodic_scheduler.cpp
)

target_include_directories(inerxia_infrastructure PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_infrastructure PUBLIC
    inerxia_application
    nlohmann_json::nlohmann_json
)

# Password hashing uses OpenSSL libcrypto (PBKDF2-HMAC-SHA256). Prefer the
# toolchain prefix ($HOME/cppenv) before the system search, keeping in sync with
# the PostgreSQL_ROOT hint used on the configure command line.
find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND AND DEFINED ENV{HOME})
    set(OPENSSL_ROOT_DIR "$ENV{HOME}/cppenv")
    find_package(OpenSSL QUIET)
endif()
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL required for operator password hashing (set OPENSSL_ROOT_DIR)")
endif()
target_link_libraries(inerxia_infrastructure PUBLIC OpenSSL::Crypto)

# Real RouterOS HTTP transport (libcurl). Only built when libcurl is available;
# unit tests exercise the gateway against a fake HttpClient, so they never need curl.
find_package(CURL QUIET)
if(CURL_FOUND)
    message(STATUS "libcurl found: building the libcurl-backed HTTP client")
    add_library(inerxia_curl_http_client STATIC
        http/curl_http_client.cpp
        mikrotik/router_gateway_factory.cpp
    )
    target_include_directories(inerxia_curl_http_client PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(inerxia_curl_http_client PUBLIC CURL::libcurl)
    target_link_libraries(inerxia_curl_http_client PUBLIC inerxia_infrastructure)
else()
    message(STATUS "libcurl NOT found: MikroTik HTTP transport (curl_http_client) not built")
endif()

# PostgreSQL adapters (libpq). Only built when libpq is available; unit tests never
# touch them, integration tests exercise them against a live server.
find_package(PostgreSQL QUIET)
if(PostgreSQL_FOUND)
    message(STATUS "libpq found: building the PostgreSQL repositories")
    add_library(inerxia_postgres STATIC
        postgres/postgres_config.cpp
        postgres/pg_connection.cpp
        postgres/migrations.cpp
        postgres/postgres_audit_repository.cpp
        postgres/postgres_contract_repository.cpp
        postgres/postgres_internet_plan_repository.cpp
        postgres/postgres_payment_repository.cpp
        postgres/postgres_subscriber_repository.cpp
        postgres/postgres_user_repository.cpp
    )
    target_include_directories(inerxia_postgres PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(inerxia_postgres PUBLIC
        inerxia_application
        PostgreSQL::PostgreSQL
    )
else()
    message(STATUS "libpq NOT found: PostgreSQL repositories not built")
endif()