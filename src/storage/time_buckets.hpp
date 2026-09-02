#pragma once

#include <cstdint>
#include <stdexcept>

namespace keystats {

inline constexpr int kMinuteSeconds = 60;
inline constexpr int kTenMinuteSeconds = 600;

inline long long AlignDown(long long value, int interval) {
    if (interval <= 0) {
        throw std::invalid_argument("interval 必须大于 0。");
    }
    const auto remainder = value % interval;
    return remainder < 0 ? value - remainder - interval : value - remainder;
}

inline long long AlignUp(long long value, int interval) {
    const auto aligned = AlignDown(value, interval);
    return aligned == value ? value : aligned + interval;
}

inline long long ToMinuteStartUnix(long long unix_seconds) {
    return AlignDown(unix_seconds, kMinuteSeconds);
}

inline long long ToTenMinuteStart(long long unix_seconds) {
    return AlignDown(unix_seconds, kTenMinuteSeconds);
}

}  // namespace keystats
