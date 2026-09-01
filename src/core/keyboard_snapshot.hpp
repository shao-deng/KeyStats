#pragma once

#include "key_id.hpp"

#include <array>
#include <cstdint>
#include <unordered_set>

namespace keystats {

struct KeyboardSnapshot {
    std::int64_t total_count = 0;
    std::array<std::int64_t, kKeyCount> counts{};
    std::unordered_set<int> pressed_keys;
    bool is_paused = false;

    [[nodiscard]] bool IsPressed(KeyId id) const {
        return pressed_keys.contains(ToIndex(id));
    }
};

}  // namespace keystats
