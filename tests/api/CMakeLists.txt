# API tests: drive the HTTP server in-process against PostgreSQL (required) and
# the live RouterOS box when MIKROTIK_* is configured (otherwise router calls
# are faked to fail, so the rest of the surface is still exercised).
#
# Run with:
#   export PGUSER=inerxia PGPASSWORD=...        # PGDATABASE defaults to inerxia_test
#   export MIKROTIK_BASE_URL=... MIKROTIK_USER=... MIKROTIK_PASSWORD=...
#   ctest --test-dir build -L api

find_package(CURL QUIET)
find_package(PostgreSQL QUIET)
if(CURL_FOUND AND PostgreSQL_FOUND)
    add_executable(api_tests
        test_api.cpp
    )

    target_link_libraries(api_tests
        PRIVATE
            inerxia_api
            inerxia_curl_http_client
            inerxia_infrastructure
            inerxia_postgres
            inerxia_application
            inerxia_domain
            GTest::gtest_main
            nlohmann_json::nlohmann_json
    )

    include(GoogleTest)
    gtest_discover_tests(api_tests
        TEST_PREFIX "api."
        DISCOVERY_TIMEOUT 30
    )

    add_test(NAME all_api COMMAND api_tests)
    set_tests_properties(all_api PROPERTIES LABELS "api")
else()
    message(STATUS
        "libcurl+libpq NOT both found: HTTP API tests not built"
    )
endif()