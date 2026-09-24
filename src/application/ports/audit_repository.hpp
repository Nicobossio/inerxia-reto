#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace inerxia::application {

// One recorded change on the system, kept for the operator to review. Audit is
// cross-cutting (not a billing entity), so it lives at the application boundary
// as a plain value instead of in the domain.
struct AuditEntry {
    std::string id;          // filled by the storage layer
    std::string occurred_at; // ISO-8601 UTC instant
    std::string actor;       // admin username, or "anonymous"
    std::string method;      // HTTP verb
    std::string path;        // request path
    int status = 0;          // HTTP response status
    std::string detail;      // short human-readable description
};

class AuditRepository {
public:
    virtual ~AuditRepository() = default;

    virtual void append(const AuditEntry& entry) = 0;
    virtual std::vector<AuditEntry> list_recent(std::size_t limit) const = 0;
};

}  // namespace inerxia::application