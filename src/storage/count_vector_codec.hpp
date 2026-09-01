#pragma once

#include "../core/key_id.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace keystats {

class CountVectorCodec {
public:
    static constexpr std::uint8_t kFormatVersion = 1;
    static constexpr int kMinimumSupportedKeyMapVersion = 1;
    static constexpr int kKeyMapVersion = 2;

    static std::vector<std::uint8_t> Encode(std::span<const std::uint32_t> counts);
    static std::array<std::uint32_t, kKeyCount> Decode(std::span<const std::uint8_t> data);
};

}  // namespace keystats
