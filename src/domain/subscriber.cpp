#include "domain/subscriber.hpp"

#include <string>
#include <utility>

#include "domain/domain_error.hpp"

namespace inerxia::domain {
namespace {

void validate_name(const std::string& name, const std::string& what) {
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        throw DomainError(what + " must not be empty");
    }
}

}  // namespace

Subscriber::Subscriber(SubscriberId id, std::string name, IPAddress static_ip)
    : id_(std::move(id)), name_(std::move(name)), static_ip_(std::move(static_ip)) {
    validate_name(name_, "Subscriber name");
}

}  // namespace inerxia::domain