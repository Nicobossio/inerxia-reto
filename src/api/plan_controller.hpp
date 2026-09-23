#pragma once

#include <httplib.h>

#include "application/use_cases/create_plan.hpp"
#include "application/use_cases/get_plan.hpp"

namespace inerxia::api {

// HTTP adapter for internet plan reads/creates.
class PlanController {
public:
    PlanController(application::CreatePlan& create, application::GetPlan& get);

    void register_routes(httplib::Server& server);

private:
    void handle_create(const httplib::Request&, httplib::Response&) const;
    void handle_get(const httplib::Request&, httplib::Response&) const;

    application::CreatePlan& create_;
    application::GetPlan& get_;
};

}  // namespace inerxia::api