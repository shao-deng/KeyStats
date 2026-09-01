#pragma once

#include "key_id.hpp"
#include "key_process_result.hpp"
#include "keyboard_snapshot.hpp"
#include "raw_key_event.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace keystats {

class KeyboardCounter {
public:
    [[nodiscard]] bool IsPaused() const noexcept;

    bool Process(std::intptr_t device_handle, const RawKeyEvent& key_event);
    KeyProcessResult ProcessDetailed(std::intptr_t device_handle, const RawKeyEvent& key_event);
    KeyProcessResult ProcessButton(std::intptr_t device_handle, KeyId key_id, bool is_break);

    void SetPaused(bool paused) noexcept;
    void ClearCounts(bool reset_pressed_state = false);
    void RemoveDevice(std::intptr_t device_handle);
    void ReleaseKey(KeyId key_id);
    KeyboardSnapshot GetSnapshot() const;

private:
    struct PressedKey {
        std::intptr_t device_handle = 0;
        KeyId key_id{};

        bool operator==(const PressedKey& other) const noexcept = default;
    };

    struct PressedKeyHash {
        std::size_t operator()(const PressedKey& key) const noexcept {
            const auto mix = static_cast<std::uint64_t>(key.device_handle) ^
                             (static_cast<std::uint64_t>(ToIndex(key.key_id)) << 1);
            return static_cast<std::size_t>(mix);
        }
    };

    mutable std::mutex pressed_gate_;
    std::unordered_set<PressedKey, PressedKeyHash> pressed_;
    mutable std::array<std::int64_t, kKeyCount> counts_{};
    mutable std::int64_t total_count_ = 0;
    mutable int is_paused_ = 0;
};

}  // namespace keystats
