#pragma once

#include <string>

#include "domain/ids.hpp"
#include "domain/ip_address.hpp"

namespace inerxia::domain {

class Subscriber {
public:
    Subscriber(SubscriberId id, std::string name, IPAddress static_ip);

    [[nodiscard]] const SubscriberId& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const IPAddress& static_ip() const noexcept { return static_ip_; }

private:
    SubscriberId id_;
    std::string name_;
    IPAddress static_ip_;
};

}  // namespace inerxia::domain