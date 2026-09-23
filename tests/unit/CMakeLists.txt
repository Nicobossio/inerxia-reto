set(UNIT_TEST_SOURCES
    test_contract.cpp
    test_internet_plan.cpp
    test_payment.cpp
    test_subscriber.cpp
    test_value_objects.cpp
)

add_executable(unit_tests
    ${UNIT_TEST_SOURCES}
)

target_link_libraries(unit_tests
    PRIVATE
        inerxia_domain
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests
    TEST_PREFIX "unit."
    DISCOVERY_TIMEOUT 30
)