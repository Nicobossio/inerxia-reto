# API layer: HTTP handlers/controllers (no business logic).

add_library(inerxia_api INTERFACE)
target_include_directories(inerxia_api INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_api INTERFACE inerxia_application)