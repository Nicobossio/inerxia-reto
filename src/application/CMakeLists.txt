# Application layer: use cases / application services.

add_library(inerxia_application INTERFACE)
target_include_directories(inerxia_application INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inerxia_application INTERFACE inerxia_domain)
