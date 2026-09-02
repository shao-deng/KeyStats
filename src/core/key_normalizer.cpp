#include "key_normalizer.hpp"

namespace keystats {
namespace {

constexpr std::uint16_t kVkPause = 0x13;
constexpr std::uint16_t kVkSnapshot = 0x2C;
constexpr std::uint16_t kVkF13 = 0x7C;
constexpr std::uint16_t kVkF24 = 0x87;

std::optional<KeyId> NormalizeExtendedVirtualKey(std::uint16_t virtual_key) {
    switch (virtual_key) {
        case 0x5F: return KeyId::Sleep;
        case 0xA6: return KeyId::BrowserBack;
        case 0xA7: return KeyId::BrowserForward;
        case 0xA8: return KeyId::BrowserRefresh;
        case 0xA9: return KeyId::BrowserStop;
        case 0xAA: return KeyId::BrowserSearch;
        case 0xAB: return KeyId::BrowserFavorites;
        case 0xAC: return KeyId::BrowserHome;
        case 0xAD: return KeyId::VolumeMute;
        case 0xAE: return KeyId::VolumeDown;
        case 0xAF: return KeyId::VolumeUp;
        case 0xB0: return KeyId::MediaNext;
        case 0xB1: return KeyId::MediaPrevious;
        case 0xB2: return KeyId::MediaStop;
        case 0xB3: return KeyId::MediaPlayPause;
        case 0xB4: return KeyId::LaunchMail;
        case 0xB5: return KeyId::LaunchMedia;
        case 0xB6: return KeyId::LaunchApplication1;
        case 0xB7: return KeyId::LaunchApplication2;
        default: return std::nullopt;
    }
}

}  // namespace

std::optional<KeyId> KeyNormalizer::Normalize(const RawKeyEvent& key_event) {
    if (key_event.virtual_key == 0 || key_event.virtual_key == 0xFF) {
        return std::nullopt;
    }
    if (key_event.virtual_key == kVkPause) {
        return KeyId::Pause;
    }
    if (key_event.virtual_key == kVkSnapshot) {
        return KeyId::PrintScreen;
    }
    if (key_event.virtual_key >= kVkF13 && key_event.virtual_key <= kVkF24) {
        return static_cast<KeyId>(ToIndex(KeyId::F13) + key_event.virtual_key - kVkF13);
    }
    if (const auto special = NormalizeExtendedVirtualKey(key_event.virtual_key)) {
        return special;
    }

    const bool extended = (key_event.flags & RawKeyFlags::E0) != RawKeyFlags::None;
    const bool e1 = (key_event.flags & RawKeyFlags::E1) != RawKeyFlags::None;

    switch (key_event.make_code) {
        case 0x01: return KeyId::Escape;
        case 0x02: return KeyId::Digit1;
        case 0x03: return KeyId::Digit2;
        case 0x04: return KeyId::Digit3;
        case 0x05: return KeyId::Digit4;
        case 0x06: return KeyId::Digit5;
        case 0x07: return KeyId::Digit6;
        case 0x08: return KeyId::Digit7;
        case 0x09: return KeyId::Digit8;
        case 0x0A: return KeyId::Digit9;
        case 0x0B: return KeyId::Digit0;
        case 0x0C: return KeyId::Minus;
        case 0x0D: return KeyId::Equal;
        case 0x0E: return KeyId::Backspace;
        case 0x0F: return KeyId::Tab;
        case 0x10: return KeyId::Q;
        case 0x11: return KeyId::W;
        case 0x12: return KeyId::E;
        case 0x13: return KeyId::R;
        case 0x14: return KeyId::T;
        case 0x15: return KeyId::Y;
        case 0x16: return KeyId::U;
        case 0x17: return KeyId::I;
        case 0x18: return KeyId::O;
        case 0x19: return KeyId::P;
        case 0x1A: return KeyId::LeftBracket;
        case 0x1B: return KeyId::RightBracket;
        case 0x1C: return extended ? KeyId::NumpadEnter : KeyId::Enter;
        case 0x1D: return extended ? KeyId::RightControl : KeyId::LeftControl;
        case 0x1E: return KeyId::A;
        case 0x1F: return KeyId::S;
        case 0x20: return KeyId::D;
        case 0x21: return KeyId::F;
        case 0x22: return KeyId::G;
        case 0x23: return KeyId::H;
        case 0x24: return KeyId::J;
        case 0x25: return KeyId::K;
        case 0x26: return KeyId::L;
        case 0x27: return KeyId::Semicolon;
        case 0x28: return KeyId::Apostrophe;
        case 0x29: return KeyId::Grave;
        case 0x2A: return extended ? std::optional<KeyId>{} : KeyId::LeftShift;
        case 0x2B: return KeyId::Backslash;
        case 0x2C: return KeyId::Z;
        case 0x2D: return KeyId::X;
        case 0x2E: return KeyId::C;
        case 0x2F: return KeyId::V;
        case 0x30: return KeyId::B;
        case 0x31: return KeyId::N;
        case 0x32: return KeyId::M;
        case 0x33: return KeyId::Comma;
        case 0x34: return KeyId::Period;
        case 0x35: return extended ? KeyId::NumpadDivide : KeyId::Slash;
        case 0x36: return extended ? std::optional<KeyId>{} : KeyId::RightShift;
        case 0x37: return extended ? KeyId::PrintScreen : KeyId::NumpadMultiply;
        case 0x38: return extended ? KeyId::RightAlt : KeyId::LeftAlt;
        case 0x39: return KeyId::Space;
        case 0x3A: return KeyId::CapsLock;
        case 0x3B: return KeyId::F1;
        case 0x3C: return KeyId::F2;
        case 0x3D: return KeyId::F3;
        case 0x3E: return KeyId::F4;
        case 0x3F: return KeyId::F5;
        case 0x40: return KeyId::F6;
        case 0x41: return KeyId::F7;
        case 0x42: return KeyId::F8;
        case 0x43: return KeyId::F9;
        case 0x44: return KeyId::F10;
        case 0x45: return e1 ? KeyId::Pause : KeyId::NumLock;
        case 0x46: return KeyId::ScrollLock;
        case 0x47: return extended ? KeyId::Home : KeyId::Numpad7;
        case 0x48: return extended ? KeyId::ArrowUp : KeyId::Numpad8;
        case 0x49: return extended ? KeyId::PageUp : KeyId::Numpad9;
        case 0x4A: return KeyId::NumpadSubtract;
        case 0x4B: return extended ? KeyId::ArrowLeft : KeyId::Numpad4;
        case 0x4C: return KeyId::Numpad5;
        case 0x4D: return extended ? KeyId::ArrowRight : KeyId::Numpad6;
        case 0x4E: return KeyId::NumpadAdd;
        case 0x4F: return extended ? KeyId::End : KeyId::Numpad1;
        case 0x50: return extended ? KeyId::ArrowDown : KeyId::Numpad2;
        case 0x51: return extended ? KeyId::PageDown : KeyId::Numpad3;
        case 0x52: return extended ? KeyId::Insert : KeyId::Numpad0;
        case 0x53: return extended ? KeyId::Delete : KeyId::NumpadDecimal;
        case 0x56: return KeyId::Oem102;
        case 0x57: return KeyId::F11;
        case 0x58: return KeyId::F12;
        case 0x5B: return KeyId::LeftWindows;
        case 0x5C: return KeyId::RightWindows;
        case 0x5D: return KeyId::Application;
        default: return std::nullopt;
    }
}

}  // namespace keystats
