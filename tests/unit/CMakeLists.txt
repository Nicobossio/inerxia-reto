add_executable(unit_tests
    test_smoke.cpp
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
