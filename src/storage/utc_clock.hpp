#pragma once

#include <chrono>

namespace keystats {

class IUtcClock {
public:
    virtual ~IUtcClock() = default;
    virtual std::chrono::system_clock::time_point UtcNow() const = 0;
};

class SystemUtcClock final : public IUtcClock {
public:
    std::chrono::system_clock::time_point UtcNow() const override {
        return std::chrono::system_clock::now();
    }
};

inline long long ToUnixSeconds(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

}  // namespace keystats
