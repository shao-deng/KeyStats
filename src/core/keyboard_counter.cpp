#include "keyboard_counter.hpp"

#include "key_normalizer.hpp"

#include <algorithm>
#include <atomic>

namespace keystats {

bool KeyboardCounter::IsPaused() const noexcept {
    return std::atomic_ref<const int>(is_paused_).load(std::memory_order_acquire) != 0;
}

bool KeyboardCounter::Process(std::intptr_t device_handle, const RawKeyEvent& key_event) {
    return ProcessDetailed(device_handle, key_event).recognized;
}

KeyProcessResult KeyboardCounter::ProcessDetailed(std::intptr_t device_handle, const RawKeyEvent& key_event) {
    const auto normalized = KeyNormalizer::Normalize(key_event);
    if (!normalized) {
        return KeyProcessResult::Unrecognized();
    }
    return ProcessButton(device_handle, *normalized, key_event.IsBreak());
}

KeyProcessResult KeyboardCounter::ProcessButton(std::intptr_t device_handle, KeyId key_id, bool is_break) {
    if (static_cast<unsigned>(ToIndex(key_id)) >= static_cast<unsigned>(kKeyCount)) {
        return KeyProcessResult::Unrecognized();
    }

    const auto tracking_device = key_id == KeyId::Tab ? 0 : device_handle;
    const PressedKey pressed_key{tracking_device, key_id};
    if (is_break) {
        std::lock_guard lock(pressed_gate_);
        pressed_.erase(pressed_key);
        return KeyProcessResult::RecognizedOnly(key_id);
    }

    bool is_new_press = false;
    {
        std::lock_guard lock(pressed_gate_);
        is_new_press = pressed_.insert(pressed_key).second;
    }

    if (is_new_press && !IsPaused()) {
        std::atomic_ref(counts_[static_cast<std::size_t>(ToIndex(key_id))]).fetch_add(1, std::memory_order_relaxed);
        std::atomic_ref(total_count_).fetch_add(1, std::memory_order_relaxed);
        return KeyProcessResult::CountedPress(key_id);
    }
    return KeyProcessResult::RecognizedOnly(key_id);
}

void KeyboardCounter::SetPaused(bool paused) noexcept {
    std::atomic_ref(is_paused_).store(paused ? 1 : 0, std::memory_order_release);
}

void KeyboardCounter::ClearCounts(bool reset_pressed_state) {
    for (auto& count : counts_) {
        std::atomic_ref(count).store(0, std::memory_order_relaxed);
    }
    std::atomic_ref(total_count_).store(0, std::memory_order_relaxed);
    if (reset_pressed_state) {
        std::lock_guard lock(pressed_gate_);
        pressed_.clear();
    }
}

void KeyboardCounter::RemoveDevice(std::intptr_t device_handle) {
    std::lock_guard lock(pressed_gate_);
    std::erase_if(pressed_, [device_handle](const PressedKey& key) {
        return key.device_handle == device_handle;
    });
}

void KeyboardCounter::ReleaseKey(KeyId key_id) {
    std::lock_guard lock(pressed_gate_);
    std::erase_if(pressed_, [key_id](const PressedKey& key) { return key.key_id == key_id; });
}

KeyboardSnapshot KeyboardCounter::GetSnapshot() const {
    KeyboardSnapshot snapshot;
    for (int index = 0; index < kKeyCount; ++index) {
        snapshot.counts[static_cast<std::size_t>(index)] =
            std::atomic_ref<const std::int64_t>(counts_[static_cast<std::size_t>(index)]).load(std::memory_order_relaxed);
    }
    snapshot.total_count = std::atomic_ref<const std::int64_t>(total_count_).load(std::memory_order_relaxed);
    snapshot.is_paused = IsPaused();
    {
        std::lock_guard lock(pressed_gate_);
        for (const auto& key : pressed_) {
            snapshot.pressed_keys.insert(ToIndex(key.key_id));
        }
    }
    return snapshot;
}

}  // namespace keystats
