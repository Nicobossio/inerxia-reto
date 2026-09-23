#pragma once

#include "application/ports/RouterGateway.h"
#include "application/ports/time_provider.hpp"
#include "application/use_cases/change_speed_profile.hpp"
#include "application/use_cases/create_contract.hpp"
#include "application/use_cases/create_plan.hpp"
#include "application/use_cases/create_subscriber.hpp"
#include "application/use_cases/evaluate_expired_contracts.hpp"
#include "application/use_cases/get_contract.hpp"
#include "application/use_cases/get_plan.hpp"
#include "application/use_cases/get_subscriber.hpp"
#include "application/use_cases/reactivate_contract.hpp"
#include "application/use_cases/register_payment.hpp"
#include "application/use_cases/suspend_contract.hpp"
#include "application/use_cases/update_contract.hpp"
#include "infrastructure/postgres/postgres_contract_repository.hpp"
#include "infrastructure/postgres/postgres_internet_plan_repository.hpp"
#include "infrastructure/postgres/postgres_payment_repository.hpp"
#include "infrastructure/postgres/postgres_subscriber_repository.hpp"

namespace inerxia::api {

// Composition root: manual dependency injection wiring concrete infrastructure
// adapters into the application services exactly once. Declared in the API
// layer next to the binary entry point; no DI framework is involved.
struct AppServices {
    infrastructure::postgres::PostgresSubscriberRepository subscribers;
    infrastructure::postgres::PostgresInternetPlanRepository plans;
    infrastructure::postgres::PostgresContractRepository contracts;
    infrastructure::postgres::PostgresPaymentRepository payments;

    application::CreateSubscriber create_subscriber;
    application::GetSubscriber get_subscriber;
    application::CreatePlan create_plan;
    application::GetPlan get_plan;
    application::CreateContract create_contract;
    application::GetContract get_contract;
    application::UpdateContract update_contract;
    application::SuspendContract suspend_contract;
    application::ReactivateContract reactivate_contract;
    application::ChangeSpeedProfile change_speed_profile;
    application::RegisterPayment register_payment;
    application::EvaluateExpiredContracts evaluate_expired_contracts;

    AppServices(infrastructure::postgres::PostgresPool& pool,
                application::RouterGateway& router, application::TimeProvider& clock)
        : subscribers{pool},
          plans{pool},
          contracts{pool},
          payments{pool},
          create_subscriber{subscribers},
          get_subscriber{subscribers},
          create_plan{plans},
          get_plan{plans},
          create_contract{contracts, subscribers, plans},
          get_contract{contracts, clock},
          update_contract{contracts},
          suspend_contract{contracts, subscribers, router},
          reactivate_contract{contracts, subscribers, router},
          change_speed_profile{contracts, subscribers, router},
          register_payment{contracts, payments, subscribers, router},
          evaluate_expired_contracts{contracts, subscribers, router, clock} {}
};

}  // namespace inerxia::api