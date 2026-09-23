# Domain layer: entities, value objects, domain events, ports.
# Must not depend on infrastructure frameworks.

add_library(inerxia_domain INTERFACE)
target_include_directories(inerxia_domain INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
