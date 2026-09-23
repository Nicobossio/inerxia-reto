#pragma once

#include <chrono>

namespace inerxia::application {

class TimeProvider {
public:
    virtual ~TimeProvider() = default;

    virtual std::chrono::year_month_day today() const = 0;
};

}  // namespace inerxia::application