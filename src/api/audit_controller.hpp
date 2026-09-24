#pragma once

#include <httplib.h>

#include "application/ports/audit_repository.hpp"

namespace inerxia::api {

// HTTP adapter for reading the recorded change audit trail.
class AuditController {
public:
    explicit AuditController(application::AuditRepository& audit);

    void register_routes(httplib::Server& server);

private:
    void handle_list(const httplib::Request&, httplib::Response&) const;

    application::AuditRepository& audit_;
};

}  // namespace inerxia::api