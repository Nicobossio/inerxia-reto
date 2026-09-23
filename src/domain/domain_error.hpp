#pragma once

#include <stdexcept>
#include <string>

namespace inerxia::domain {

class DomainError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace inerxia::domain