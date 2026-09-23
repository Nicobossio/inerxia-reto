#pragma once

#include <cstdint>

namespace inerxia::domain {

class Money {
public:
    explicit constexpr Money(std::int64_t cents) noexcept : cents_(cents) {}

    static constexpr Money from_cents(std::int64_t cents) noexcept { return Money{cents}; }

    [[nodiscard]] constexpr std::int64_t cents() const noexcept { return cents_; }

    [[nodiscard]] constexpr bool is_positive() const noexcept { return cents_ > 0; }

    [[nodiscard]] constexpr bool is_non_negative() const noexcept { return cents_ >= 0; }

    constexpr Money& operator+=(Money other) noexcept {
        cents_ += other.cents_;
        return *this;
    }

    constexpr Money& operator-=(Money other) noexcept {
        cents_ -= other.cents_;
        return *this;
    }

    friend constexpr Money operator+(Money lhs, Money rhs) noexcept {
        return Money{lhs.cents_ + rhs.cents_};
    }

    friend constexpr Money operator-(Money lhs, Money rhs) noexcept {
        return Money{lhs.cents_ - rhs.cents_};
    }

    friend constexpr bool operator==(const Money&, const Money&) noexcept = default;

private:
    std::int64_t cents_;
};

}  // namespace inerxia::domain