set(UNIT_TEST_SOURCES
    test_application_use_cases.cpp
    test_contract.cpp
    test_contract_rules.cpp
    test_internet_plan.cpp
    test_mikrotik_env.cpp
    test_mikrotik_router_gateway.cpp
    test_payment.cpp
    test_subscriber.cpp
    test_value_objects.cpp
)

add_executable(unit_tests
    ${UNIT_TEST_SOURCES}
)

target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(unit_tests
    PRIVATE
        inerxia_application
        inerxia_domain
        inerxia_infrastructure
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests
    TEST_PREFIX "unit."
    DISCOVERY_TIMEOUT 30
)

# Aggregate labeled entry so `ctest -L unit` works.
add_test(NAME all_unit COMMAND unit_tests)
set_tests_properties(all_unit PROPERTIES LABELS "unit")