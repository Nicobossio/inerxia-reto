#pragma once

#include <string>

namespace inerxia::domain {

class IPAddress {
public:
    explicit IPAddress(std::string value);

    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    friend bool operator==(const IPAddress&, const IPAddress&) = default;

private:
    std::string value_;
};

}  // namespace inerxia::domain