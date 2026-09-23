# Application layer: use cases / application services.

add_library(inerxia_application INTERFACE)
target_include_directories(inerxia_application INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_application INTERFACE inerxia_domain)