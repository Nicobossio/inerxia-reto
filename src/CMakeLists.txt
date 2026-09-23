# Layered structure (hexagonal architecture).
# Targets are added incrementally; only test scaffolding exists for now.

include(FetchContent)

# Shared third-party dependency: nlohmann JSON is used by the infrastructure and
# API layers (never by domain). Declared once here so every subtree can consume
# the target it defines.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(domain)
add_subdirectory(application)
add_subdirectory(infrastructure)
add_subdirectory(api)
