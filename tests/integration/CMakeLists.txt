# Integration tests: live MikroTik/RouterOS (env-gated) and PostgreSQL.
#
# The MikroTik tests talk to a REAL RouterOS box through the production libcurl
# transport and the production gateway (no fakes). They GTEST_SKIP when
# MIKROTIK_BASE_URL / MIKROTIK_USER / MIKROTIK_PASSWORD are not set, so plain
# `ctest` stays green without a router. Run them with:
#   export MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest
#   export MIKROTIK_USER=...
#   export MIKROTIK_PASSWORD=...
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

    include(GoogleTest)
    gtest_discover_tests(integration_tests
        TEST_PREFIX "integration."
        DISCOVERY_TIMEOUT 30
    )

    add_test(NAME all_integration COMMAND integration_tests)
    set_tests_properties(all_integration PROPERTIES LABELS "integration")
else()
    message(STATUS "libcurl NOT found: MikroTik live integration tests not built")
endif()