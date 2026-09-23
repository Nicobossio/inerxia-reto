#pragma once

#include <cstdlib>
#include <optional>
#include <string>

// RAII guard that sets an environment variable for the duration of a test and
// restores the previous value (or removes it) on destruction.
class EnvGuard {
public:
    EnvGuard(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name_)) {
            saved_ = previous;
        }
        if (value == nullptr) {
            unsetenv(name_);
        } else {
            setenv(name_, value, 1);
        }
    }

    ~EnvGuard() {
        if (saved_.has_value()) {
            setenv(name_, saved_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> saved_;
};