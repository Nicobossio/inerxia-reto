# Infrastructure layer: PostgreSQL, HTTP, MikroTik adapters.
# Only this layer may depend on HTTP, JSON, libcurl or RouterOS specifics.

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(inerxia_infrastructure STATIC
    mikrotik/MikrotikRouterGateway.cpp
)

target_include_directories(inerxia_infrastructure PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_infrastructure PUBLIC
    inerxia_application
    nlohmann_json::nlohmann_json
)

# Real RouterOS HTTP transport (libcurl). Only built when libcurl is available;
# unit tests exercise the gateway against a fake HttpClient, so they never need curl.
find_package(CURL QUIET)
if(CURL_FOUND)
    message(STATUS "libcurl found: building the libcurl-backed HTTP client")
    add_library(inerxia_curl_http_client STATIC
        http/curl_http_client.cpp
        mikrotik/router_gateway_factory.cpp
    )
    target_include_directories(inerxia_curl_http_client PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(inerxia_curl_http_client PUBLIC CURL::libcurl)
else()
    message(STATUS "libcurl NOT found: MikroTik HTTP transport (curl_http_client) not built")
endif()