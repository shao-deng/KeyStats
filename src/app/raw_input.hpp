#pragma once

#include "../core/key_id.hpp"
#include "../core/keyboard_counter.hpp"

#include <functional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace keystats {

class RawInputSource {
public:
    RawInputSource(HWND window, KeyboardCounter& counter, std::function<void(KeyId)> counted_sink);
    ~RawInputSource();

    RawInputSource(const RawInputSource&) = delete;
    RawInputSource& operator=(const RawInputSource&) = delete;

    bool HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    void ReconcileSystemShortcutState();
    void ClearDiagnostics();
    void OnAltSamplingTick();

    const std::string& LastInputDescription() const { return last_input_description_; }
    int UnrecognizedMakeCount() const { return unrecognized_make_count_; }
    const std::string& LastUnrecognizedDescription() const { return last_unrecognized_description_; }

private:
    void ProcessRawInput(LPARAM raw_input_handle);
    void UpdateSystemShortcutSampling(const std::optional<KeyId>& key_id, bool is_break);
    void ReleaseIfPhysicallyUp(KeyId key_id, int virtual_key);

    HWND window_;
    KeyboardCounter& counter_;
    std::function<void(KeyId)> counted_sink_;
    std::string last_input_description_ = "等待键盘输入…";
    std::string last_unrecognized_description_;
    int unrecognized_make_count_ = 0;
    bool sampled_alt_tab_down_ = false;
    bool disposed_ = false;
};

}  // namespace keystats
