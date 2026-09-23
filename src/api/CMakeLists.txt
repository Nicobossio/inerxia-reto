# API layer: HTTP handlers/controllers (no business logic).
#
# Design decision: cpp-httplib (yhirose) was chosen as the lightweight HTTP
# framework. It is header-only, needs no transport dependencies beyond the
# standard sockets/pthreads, and fits the challenge's "lightweight framework"
# requirement better than heavier options (Drogon/Pistache/Beast).
# It is fetched as a pinned third-party dep via FetchContent below.

include(FetchContent)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpp_httplib)

add_library(inerxia_api STATIC
    api_server.cpp
    contract_controller.cpp
    health_controller.cpp
    plan_controller.cpp
    subscriber_controller.cpp
)

target_include_directories(inerxia_api PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_api PUBLIC
    inerxia_application
    nlohmann_json::nlohmann_json
)
# Public headers (api_server.hpp) include <httplib.h>, so the target must be
# PUBLIC for consuming executables/tests to compile against this layer.
target_link_libraries(inerxia_api PUBLIC httplib::httplib)

# The composition root ties the concrete PostgreSQL repositories together, so it
# is only usable when libpq is available (same policy as the infrastructure
# layer). AppServices is a header-only struct (manual DI, no framework).
find_package(PostgreSQL QUIET)
if(PostgreSQL_FOUND)
    target_link_libraries(inerxia_api PUBLIC inerxia_postgres)
endif()

# The HTTP server executable additionally requires the real libcurl MikroTik
# transport. It fails fast at runtime when PostgreSQL or MikroTik credentials
# are missing from the environment.
find_package(CURL QUIET)
if(PostgreSQL_FOUND AND CURL_FOUND)
    add_executable(inerxia_server main.cpp)
    target_link_libraries(inerxia_server PRIVATE
        inerxia_api
        inerxia_infrastructure
        inerxia_curl_http_client
    )
else()
    message(STATUS
        "libpq+libcurl NOT both found: inerxia_server executable not built"
    )
endif()