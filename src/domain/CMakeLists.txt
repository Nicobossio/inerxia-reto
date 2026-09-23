# Domain layer: entities, value objects, domain events, ports.
# Must not depend on infrastructure frameworks.

add_library(inerxia_domain STATIC
    contract.cpp
    internet_plan.cpp
    ip_address.cpp
    payment.cpp
    speed_profile.cpp
    subscriber.cpp
)

target_include_directories(inerxia_domain PUBLIC ${PROJECT_SOURCE_DIR}/src)