# Integration tests: live MikroTik/RouterOS and PostgreSQL (both env-gated).
#
# MikroTik tests talk to a REAL RouterOS box through the production libcurl
# transport and the production gateway (no fakes). They GTEST_SKIP when
# MIKROTIK_BASE_URL / MIKROTIK_USER / MIKROTIK_PASSWORD are not set.
# Run with:
#   export MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest
#   export MIKROTIK_USER=...
#   export MIKROTIK_PASSWORD=...
#
# PostgreSQL tests run the repositories against a REAL server but on a database
# that is separate from production (PGDATABASE defaults to inerxia_test). They
# GTEST_SKIP when PGUSER / PGPASSWORD are unset or the server is unreachable.
# Run with:
#   export PGUSER=inerxia PGPASSWORD=...
#   ctest --test-dir build -L integration

find_package(CURL QUIET)
if(CURL_FOUND)
    add_executable(integration_tests
        test_mikrotik_gateway_lab.cpp
    )

    target_link_libraries(integration_tests
        PRIVATE
            inerxia_curl_http_client
            inerxia_infrastructure
            inerxia_application
            inerxia_domain
            GTest::gtest_main
            nlohmann_json::nlohmann_json
    )
else()
    message(STATUS
        "libcurl NOT found: MikroTik live integration tests not built"
    )
endif()

find_package(PostgreSQL QUIET)
if(PostgreSQL_FOUND)
    add_executable(postgres_repository_tests
        test_postgres_repositories.cpp
    )

    target_link_libraries(postgres_repository_tests
        PRIVATE
            inerxia_postgres
            inerxia_infrastructure
            inerxia_application
            inerxia_domain
            GTest::gtest_main
    )
else()
    message(STATUS
        "libpq NOT found: PostgreSQL repository integration tests not built"
    )
endif()

# Every test executable gets its tests discovered under a distinct prefix and one
# aggregate entry labeled `integration` so `ctest -L integration` runs them all.
include(GoogleTest)
foreach(target integration_tests postgres_repository_tests)
    if(TARGET ${target})
        gtest_discover_tests(${target}
            TEST_PREFIX "integration."
            DISCOVERY_TIMEOUT 30
        )
        add_test(NAME all_${target} COMMAND ${target})
        set_tests_properties(all_${target} PROPERTIES LABELS "integration")
    endif()
endforeach()