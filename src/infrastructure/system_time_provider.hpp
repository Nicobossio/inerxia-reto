#pragma once

#include <chrono>

#include "application/ports/time_provider.hpp"

namespace inerxia::infrastructure {

// TimeProvider backed by the system clock (UTC calendar date).
class SystemTimeProvider final : public application::TimeProvider {
public:
    std::chrono::year_month_day today() const override {
        return std::chrono::year_month_day{
            std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    }
};

}  // namespace inerxia::infrastructure