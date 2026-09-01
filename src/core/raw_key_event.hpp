#pragma once

#include <cstdint>

namespace keystats {

enum class RawKeyFlags : std::uint16_t {
    None = 0,
    Break = 0x0001,
    E0 = 0x0002,
    E1 = 0x0004,
};

inline constexpr RawKeyFlags operator|(RawKeyFlags a, RawKeyFlags b) noexcept {
    return static_cast<RawKeyFlags>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

inline constexpr RawKeyFlags operator&(RawKeyFlags a, RawKeyFlags b) noexcept {
    return static_cast<RawKeyFlags>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

struct RawKeyEvent {
    std::uint16_t make_code = 0;
    RawKeyFlags flags = RawKeyFlags::None;
    std::uint16_t virtual_key = 0;

    [[nodiscard]] bool IsBreak() const noexcept {
        return (flags & RawKeyFlags::Break) != RawKeyFlags::None;
    }
};

}  // namespace keystats
