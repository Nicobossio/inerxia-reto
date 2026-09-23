# Infrastructure layer: PostgreSQL, HTTP, MikroTik adapters.

add_library(inerxia_infrastructure INTERFACE)
target_include_directories(inerxia_infrastructure INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_infrastructure INTERFACE inerxia_application)