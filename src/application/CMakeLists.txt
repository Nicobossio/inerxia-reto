# Application layer: use cases / application services.
# Depends only on ports (interfaces); no infrastructure frameworks here.

add_library(inerxia_application STATIC
    use_cases/change_speed_profile.cpp
    use_cases/create_contract.cpp
    use_cases/create_plan.cpp
    use_cases/create_subscriber.cpp
    use_cases/evaluate_expired_contracts.cpp
    use_cases/get_contract.cpp
    use_cases/get_plan.cpp
    use_cases/get_subscriber.cpp
    use_cases/list_contracts.cpp
    use_cases/list_subscribers.cpp
    use_cases/reactivate_contract.cpp
    use_cases/register_payment.cpp
    use_cases/suspend_contract.cpp
    use_cases/update_contract.cpp
    services/auth_service.cpp
)

target_include_directories(inerxia_application PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(inerxia_application PUBLIC inerxia_domain)