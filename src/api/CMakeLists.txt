# API layer: HTTP handlers/controllers (no business logic).

add_library(inerxia_api INTERFACE)
target_include_directories(inerxia_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inerxia_api INTERFACE inerxia_application)
