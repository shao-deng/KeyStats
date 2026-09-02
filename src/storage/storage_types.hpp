#pragma once

#include "../core/key_id.hpp"

#include <array>
#include <cstdint>
#include <numeric>

namespace keystats {

struct MinuteBucketSnapshot {
    long long bucket_start_utc = 0;
    std::array<std::uint32_t, kKeyCount> counts{};

    [[nodiscard]] long long TotalCount() const {
        return std::accumulate(counts.begin(), counts.end(), 0LL);
    }
};

struct StatisticsAggregate {
    long long start_utc = 0;
    long long end_utc = 0;
    std::array<std::uint64_t, kKeyCount> counts{};

    [[nodiscard]] std::uint64_t TotalCount() const {
        return std::accumulate(counts.begin(), counts.end(), 0ULL);
    }
};

struct StoredDataRange {
    long long start_utc = 0;
    long long end_utc = 0;
};

struct TenMinuteBucketTotal {
    long long bucket_start_utc = 0;
    std::uint64_t total_count = 0;
};

}  // namespace keystats
