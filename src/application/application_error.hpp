#pragma once

#include <stdexcept>

namespace inerxia::application {

class EntityNotFoundError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace inerxia::application