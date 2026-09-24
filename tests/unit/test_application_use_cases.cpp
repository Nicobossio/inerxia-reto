#include <gtest/gtest.h>

#include <chrono>

#include "application/application_error.hpp"
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
#include "domain/domain_error.hpp"
#include "domain/money.hpp"
#include "application_fakes.hpp"

namespace inerxia::application::test {
namespace {

using namespace std::chrono;
using domain::Contract;
using domain::ContractId;
using domain::ContractStatus;
using domain::InternetPlan;
using domain::IPAddress;
using domain::Money;
using domain::PlanId;
using domain::SpeedProfile;
using domain::Subscriber;
using domain::SubscriberId;

constexpr auto kBillingStart = year{2026}/9/1;
constexpr auto kDueDate = year{2026}/10/1;
constexpr auto kPrice = Money::from_cents(15000);

class ApplicationUseCaseTest : public ::testing::Test {
protected:
    ApplicationUseCaseTest() : clock_(kBillingStart) {}

    void seed_subscriber(const SubscriberId& id = SubscriberId{"sub-1"},
                         const IPAddress& ip = IPAddress{"10.20.30.40"}) {
        subscribers_.save(Subscriber{id, "Ana", ip});
    }

    void seed_plan(const PlanId& id = PlanId{"plan-1"},
                   const SpeedProfile& speed = SpeedProfile{300, 150},
                   const Money& price = kPrice) {
        plans_.save(InternetPlan{id, "Fibra 300", speed, price});
    }

    Contract create_contract(const SubscriberId& subscriber = SubscriberId{"sub-1"},
                             const PlanId& plan = PlanId{"plan-1"},
                             year_month_day billing_start = kBillingStart,
                             year_month_day due_date = kDueDate) {
        CreateContract use_case{contracts_, subscribers_, plans_};
        return use_case(
            CreateContractCommand{subscriber, plan, billing_start, due_date});
    }

    FakeTimeProvider clock_;
    FakeRouterGateway router_;
    InMemoryContractRepository contracts_;
    InMemoryPaymentRepository payments_;
    InMemorySubscriberRepository subscribers_;
    InMemoryInternetPlanRepository plans_;
};

TEST_F(ApplicationUseCaseTest, CreateContractProducesActiveContractFromPlanAndSubscriber) {
    seed_subscriber();
    seed_plan(PlanId{"plan-1"}, SpeedProfile{600, 300}, Money::from_cents(20000));

    auto contract = create_contract();

    EXPECT_EQ(contract.subscriber_id(), SubscriberId{"sub-1"});
    EXPECT_EQ(contract.plan_id(), PlanId{"plan-1"});
    EXPECT_EQ(contract.speed_profile(), (SpeedProfile{600, 300}));
    EXPECT_EQ(contract.price_per_period(), Money::from_cents(20000));
    EXPECT_EQ(contract.status_as_of(kBillingStart), ContractStatus::Active);
    EXPECT_TRUE(contracts_.find_by_id(contract.id()).has_value());
}

TEST_F(ApplicationUseCaseTest, CreateContractWithMissingSubscriberThrows) {
    seed_plan();

    EXPECT_THROW(create_contract(), EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, CreateContractWithMissingPlanThrows) {
    seed_subscriber();

    EXPECT_THROW(create_contract(), EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, CreateContractWithInvalidDatesPropagatesDomainError) {
    seed_subscriber();
    seed_plan();

    CreateContract use_case{contracts_, subscribers_, plans_};

    EXPECT_THROW(use_case(CreateContractCommand{SubscriberId{"sub-1"}, PlanId{"plan-1"},
                                                kDueDate, kBillingStart}),
                 domain::DomainError);
}

TEST_F(ApplicationUseCaseTest, GetContractComputesStatusFromTimeProvider) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    GetContract use_case{contracts_, clock_};

    const auto before_due = use_case(contract.id());
    EXPECT_EQ(before_due.status, ContractStatus::Active);
    EXPECT_EQ(before_due.contract.due_date(), kDueDate);
}

TEST_F(ApplicationUseCaseTest, GetContractShowsOverdueWhenTodayIsPastDueDate) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    clock_.set_today(year{2026}/10/2);

    GetContract use_case{contracts_, clock_};
    EXPECT_EQ(use_case(contract.id()).status, ContractStatus::Overdue);
}

TEST_F(ApplicationUseCaseTest, GetContractShowsSuspendedAfterSuspension) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    SuspendContract use_case{contracts_, subscribers_, router_};
    use_case(SuspendContractCommand{contract.id()});

    GetContract get{contracts_, clock_};
    EXPECT_EQ(get(contract.id()).status, ContractStatus::Suspended);
}

TEST_F(ApplicationUseCaseTest, GetContractWithUnknownIdThrows) {
    GetContract use_case{contracts_, clock_};

    EXPECT_THROW(use_case(ContractId{"unknown"}), EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, UpdateContractReschedulesDueDateAndPersists) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    UpdateContract use_case{contracts_};
    auto updated = use_case(UpdateContractCommand{contract.id(), year{2026}/12/1});

    EXPECT_EQ(updated.due_date(), year{2026}/12/1);
    EXPECT_GT(sys_days{updated.due_date()}, sys_days{updated.billing_start()});

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->due_date(), year{2026}/12/1);
}

TEST_F(ApplicationUseCaseTest, UpdateContractWithInvalidDueDateThrows) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    UpdateContract use_case{contracts_};

    EXPECT_THROW(use_case(UpdateContractCommand{contract.id(), kBillingStart}),
                 domain::DomainError);
}

TEST_F(ApplicationUseCaseTest, UpdateContractWithUnknownIdThrows) {
    UpdateContract use_case{contracts_};

    EXPECT_THROW(use_case(UpdateContractCommand{ContractId{"unknown"}, year{2026}/12/1}),
                 EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, SuspendContractSuspendsAndDisablesService) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    SuspendContract use_case{contracts_, subscribers_, router_};
    auto suspended = use_case(SuspendContractCommand{contract.id()});

    EXPECT_TRUE(suspended.is_suspended());
    ASSERT_EQ(router_.calls_of(RouterCall::Kind::DisableUser), 1U);
    ASSERT_EQ(router_.calls().size(), 1U);
    EXPECT_EQ(router_.calls().front().ip, "10.20.30.40");

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->is_suspended());
}

TEST_F(ApplicationUseCaseTest, SuspendContractOnSuspendedContractThrows) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    SuspendContract use_case{contracts_, subscribers_, router_};
    use_case(SuspendContractCommand{contract.id()});

    EXPECT_THROW(use_case(SuspendContractCommand{contract.id()}), domain::DomainError);
}

TEST_F(ApplicationUseCaseTest, SuspendContractWithUnknownIdThrows) {
    SuspendContract use_case{contracts_, subscribers_, router_};

    EXPECT_THROW(use_case(SuspendContractCommand{ContractId{"unknown"}}),
                 EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, ReactivateContractReactivatesAndEnablesService) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    SuspendContract suspend_uc{contracts_, subscribers_, router_};
    suspend_uc(SuspendContractCommand{contract.id()});

    ReactivateContract use_case{contracts_, subscribers_, router_};
    auto reactivated = use_case(ReactivateContractCommand{contract.id()});

    EXPECT_FALSE(reactivated.is_suspended());
    ASSERT_EQ(router_.calls_of(RouterCall::Kind::EnableUser), 1U);
    EXPECT_EQ(router_.calls_of(RouterCall::Kind::DisableUser), 1U);

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->is_suspended());
}

TEST_F(ApplicationUseCaseTest, ReactivateContractOnActiveContractThrows) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    ReactivateContract use_case{contracts_, subscribers_, router_};

    EXPECT_THROW(use_case(ReactivateContractCommand{contract.id()}), domain::DomainError);
}

TEST_F(ApplicationUseCaseTest, RegisterPaymentSavesPaymentAndSettlesDebtWithoutRouter) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    RegisterPayment use_case{contracts_, payments_, subscribers_, router_};
    auto payment =
        use_case(RegisterPaymentCommand{contract.id(), kPrice, year{2026}/9/15});

    EXPECT_EQ(payment.contract_id(), contract.id());
    EXPECT_EQ(payment.amount(), kPrice);
    EXPECT_EQ(payments_.saved_count(), 1U);

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->balance_as_of(year{2026}/10/2), Money::from_cents(0));
    EXPECT_EQ(stored->status_as_of(year{2026}/10/2), ContractStatus::Active);

    EXPECT_TRUE(router_.calls().empty());
}

TEST_F(ApplicationUseCaseTest, RegisterPaymentOnAutoSuspendedContractReactivatesAndEnables) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    clock_.set_today(year{2026}/10/2);
    EvaluateExpiredContracts evaluator{contracts_, subscribers_, router_, clock_};
    const auto suspended_ids = evaluator();
    ASSERT_EQ(suspended_ids.size(), 1U);
    ASSERT_EQ(router_.calls_of(RouterCall::Kind::DisableUser), 1U);
    router_.clear_calls();

    RegisterPayment use_case{contracts_, payments_, subscribers_, router_};
    use_case(RegisterPaymentCommand{contract.id(), kPrice, year{2026}/10/2});

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->is_suspended());
    ASSERT_EQ(router_.calls_of(RouterCall::Kind::EnableUser), 1U);
    EXPECT_EQ(router_.calls().front().ip, "10.20.30.40");
}

TEST_F(ApplicationUseCaseTest, RegisterPaymentOnUnknownContractThrows) {
    seed_subscriber();

    RegisterPayment use_case{contracts_, payments_, subscribers_, router_};

    EXPECT_THROW(use_case(RegisterPaymentCommand{ContractId{"unknown"}, kPrice,
                                                 year{2026}/9/15}),
                 EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, ChangeSpeedProfileUpdatesAndNotifiesRouter) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    ChangeSpeedProfile use_case{contracts_, subscribers_, router_};
    auto updated =
        use_case(ChangeSpeedProfileCommand{contract.id(), SpeedProfile{600, 300}});

    EXPECT_EQ(updated.speed_profile(), (SpeedProfile{600, 300}));

    ASSERT_EQ(router_.calls_of(RouterCall::Kind::ChangeSpeedProfile), 1U);
    const auto& call = router_.calls().front();
    EXPECT_EQ(call.ip, "10.20.30.40");
    ASSERT_TRUE(call.speed.has_value());
    EXPECT_EQ(*call.speed, (SpeedProfile{600, 300}));

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->speed_profile(), (SpeedProfile{600, 300}));
}

TEST_F(ApplicationUseCaseTest, ChangeSpeedProfileWithSameMegasDoesNotNotifyRouter) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    ChangeSpeedProfile use_case{contracts_, subscribers_, router_};
    use_case(ChangeSpeedProfileCommand{contract.id(), SpeedProfile{300, 150}});

    EXPECT_TRUE(router_.calls().empty());
}

TEST_F(ApplicationUseCaseTest, ChangeSpeedProfileOnUnknownContractThrows) {
    seed_subscriber();

    ChangeSpeedProfile use_case{contracts_, subscribers_, router_};

    EXPECT_THROW(use_case(ChangeSpeedProfileCommand{ContractId{"unknown"},
                                                    SpeedProfile{600, 300}}),
                 EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsSuspendsOnlyOverdueAndDisables) {
    seed_subscriber();
    seed_plan();
    auto overdue = create_contract();

    auto paid_seed = create_contract(SubscriberId{"sub-1"}, PlanId{"plan-1"});
    RegisterPayment pay_uc{contracts_, payments_, subscribers_, router_};
    pay_uc(RegisterPaymentCommand{paid_seed.id(), kPrice, year{2026}/9/15});

    clock_.set_today(year{2026}/10/2);

    EvaluateExpiredContracts use_case{contracts_, subscribers_, router_, clock_};
    const auto suspended_ids = use_case();

    ASSERT_EQ(suspended_ids.size(), 1U);
    EXPECT_EQ(suspended_ids.front(), overdue.id());

    auto overdue_stored = contracts_.find_by_id(overdue.id());
    ASSERT_TRUE(overdue_stored.has_value());
    EXPECT_TRUE(overdue_stored->is_suspended());

    auto paid_stored = contracts_.find_by_id(paid_seed.id());
    ASSERT_TRUE(paid_stored.has_value());
    EXPECT_FALSE(paid_stored->is_suspended());
    EXPECT_EQ(paid_stored->status_as_of(year{2026}/10/2), ContractStatus::Active);

    ASSERT_EQ(router_.calls_of(RouterCall::Kind::DisableUser), 1U);
    EXPECT_EQ(router_.calls().front().ip, "10.20.30.40");
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsIsIdempotentForSuspendedContracts) {
    seed_subscriber();
    seed_plan();
    create_contract();

    clock_.set_today(year{2026}/10/2);
    EvaluateExpiredContracts use_case{contracts_, subscribers_, router_, clock_};

    const auto first = use_case();
    ASSERT_EQ(first.size(), 1U);
    const auto disabled_after_first = router_.calls_of(RouterCall::Kind::DisableUser);

    const auto second = use_case();
    EXPECT_TRUE(second.empty());
    EXPECT_EQ(router_.calls_of(RouterCall::Kind::DisableUser), disabled_after_first);
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsLeavesCurrentContractUntouched) {
    seed_subscriber();
    seed_plan();
    // Due on 2026-10-01 while "today" is 2026-09-15: the contract is still
    // current (active) and must not be suspended.
    clock_.set_today(year{2026}/9/15);
    auto contract = create_contract();

    EvaluateExpiredContracts use_case{contracts_, subscribers_, router_, clock_};
    const auto suspended_ids = use_case();

    EXPECT_TRUE(suspended_ids.empty());
    EXPECT_TRUE(router_.calls().empty());

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->is_suspended());
    EXPECT_EQ(stored->status_as_of(year{2026}/9/15), ContractStatus::Active);
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsSkipsOverdueContractWithValidPayment) {
    seed_subscriber();
    seed_plan();
    // Payment registered before the due date and covering the full period: the
    // contract is overdue by date but has no unpaid balance, so it stays active.
    auto contract = create_contract();
    RegisterPayment pay_uc{contracts_, payments_, subscribers_, router_};
    pay_uc(RegisterPaymentCommand{contract.id(), kPrice, year{2026}/9/15});

    clock_.set_today(year{2026}/10/2);
    EvaluateExpiredContracts use_case{contracts_, subscribers_, router_, clock_};

    const auto suspended_ids = use_case();

    EXPECT_TRUE(suspended_ids.empty());
    EXPECT_TRUE(router_.calls_of(RouterCall::Kind::DisableUser) == 0U);
    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->status_as_of(year{2026}/10/2), ContractStatus::Active);
    EXPECT_FALSE(stored->is_suspended());
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsSkipsAlreadySuspendedContract) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();
    // Suspended before the sweep (e.g. manually or by an earlier run).
    SuspendContract suspend_uc{contracts_, subscribers_, router_};
    suspend_uc(SuspendContractCommand{contract.id()});
    const auto disable_calls_before = router_.calls_of(RouterCall::Kind::DisableUser);

    clock_.set_today(year{2026}/10/2);
    EvaluateExpiredContracts use_case{contracts_, subscribers_, router_, clock_};
    const auto suspended_ids = use_case();

    EXPECT_TRUE(suspended_ids.empty());
    // Not re-suspended, so the router is not called a second time.
    EXPECT_EQ(router_.calls_of(RouterCall::Kind::DisableUser), disable_calls_before);

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->is_suspended());
    EXPECT_EQ(stored->status_as_of(year{2026}/10/2), ContractStatus::Suspended);
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsRouterFailureKeepsMutationAndRetryIsSafe) {
    seed_subscriber();
    seed_plan();
    auto contract = create_contract();

    ThrowingRouterGateway failing_router;
    clock_.set_today(year{2026}/10/2);
    EvaluateExpiredContracts use_case{contracts_, subscribers_, failing_router, clock_};

    // The suspension is persisted BEFORE the router call, so the mutation
    // survives a router failure; the failure still propagates to the caller.
    EXPECT_THROW(use_case(), std::runtime_error);
    EXPECT_EQ(failing_router.attempts(), 1);

    auto stored = contracts_.find_by_id(contract.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->is_suspended());
    EXPECT_EQ(stored->status_as_of(year{2026}/10/2), ContractStatus::Suspended);

    // Idempotence after the failure: the contract is already suspended, so a
    // retry does not re-attempt the router.
    const auto second = use_case();
    EXPECT_TRUE(second.empty());
    EXPECT_EQ(failing_router.attempts(), 1);
}

TEST_F(ApplicationUseCaseTest, EvaluateExpiredContractsSkipsContractWithoutSubscriber) {
    seed_plan();
    contracts_.save(Contract{ContractId{"ct-x"}, SubscriberId{"missing"}, PlanId{"plan-1"},
                             SpeedProfile{300, 150}, kBillingStart, kDueDate, kPrice});

    clock_.set_today(year{2026}/10/2);
    EvaluateExpiredContracts use_case{contracts_, subscribers_, router_, clock_};

    const auto suspended_ids = use_case();

    EXPECT_TRUE(suspended_ids.empty());
    EXPECT_TRUE(router_.calls().empty());

    auto stored = contracts_.find_by_id(ContractId{"ct-x"});
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->is_suspended());
}

TEST_F(ApplicationUseCaseTest, CreateSubscriberAssignsIdAndPersists) {
    CreateSubscriber use_case{subscribers_};

    auto subscriber = use_case(CreateSubscriberCommand{"Ana", IPAddress{"10.1.2.3"}});

    EXPECT_FALSE(subscriber.id().value().empty());
    EXPECT_EQ(subscriber.name(), "Ana");
    EXPECT_EQ(subscriber.static_ip(), (IPAddress{"10.1.2.3"}));

    auto stored = subscribers_.find_by_id(subscriber.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->name(), "Ana");
}

TEST_F(ApplicationUseCaseTest, CreateSubscriberWithInvalidIpThrowsDomainError) {
    CreateSubscriber use_case{subscribers_};

    EXPECT_THROW(use_case(CreateSubscriberCommand{"Ana", IPAddress{"999.1.1.1"}}),
                 domain::DomainError);
}

TEST_F(ApplicationUseCaseTest, GetSubscriberReturnsStoredSubscriber) {
    seed_subscriber(SubscriberId{"sub-9"}, IPAddress{"10.1.2.3"});

    GetSubscriber use_case{subscribers_};

    auto subscriber = use_case(SubscriberId{"sub-9"});
    EXPECT_EQ(subscriber.name(), "Ana");
    EXPECT_EQ(subscriber.static_ip(), (IPAddress{"10.1.2.3"}));
}

TEST_F(ApplicationUseCaseTest, GetSubscriberWithUnknownIdThrows) {
    GetSubscriber use_case{subscribers_};

    EXPECT_THROW(use_case(SubscriberId{"unknown"}), EntityNotFoundError);
}

TEST_F(ApplicationUseCaseTest, CreatePlanAssignsIdAndPersists) {
    CreatePlan use_case{plans_};

    auto plan = use_case(CreatePlanCommand{"Fibra 600", SpeedProfile{600, 300},
                                           Money::from_cents(25000)});

    EXPECT_FALSE(plan.id().value().empty());
    EXPECT_EQ(plan.name(), "Fibra 600");
    EXPECT_EQ(plan.speed(), (SpeedProfile{600, 300}));
    EXPECT_EQ(plan.monthly_price(), Money::from_cents(25000));

    auto stored = plans_.find_by_id(plan.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->monthly_price(), Money::from_cents(25000));
}

TEST_F(ApplicationUseCaseTest, CreatePlanWithInvalidSpeedThrowsDomainError) {
    CreatePlan use_case{plans_};

    EXPECT_THROW(use_case(CreatePlanCommand{"Bad", SpeedProfile{0, 300},
                                            Money::from_cents(25000)}),
                 domain::DomainError);
}

TEST_F(ApplicationUseCaseTest, GetPlanReturnsStoredPlan) {
    seed_plan(PlanId{"plan-z"}, SpeedProfile{600, 300}, Money::from_cents(25000));

    GetPlan use_case{plans_};

    auto plan = use_case(PlanId{"plan-z"});
    EXPECT_EQ(plan.name(), "Fibra 300");
    EXPECT_EQ(plan.speed(), (SpeedProfile{600, 300}));
    EXPECT_EQ(plan.monthly_price(), Money::from_cents(25000));
}

TEST_F(ApplicationUseCaseTest, GetPlanWithUnknownIdThrows) {
    GetPlan use_case{plans_};

    EXPECT_THROW(use_case(PlanId{"unknown"}), EntityNotFoundError);
}

}  // namespace
}  // namespace inerxia::application::test