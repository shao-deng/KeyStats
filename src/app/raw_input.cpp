#include "raw_input.hpp"

#include "../core/key_catalog.hpp"
#include "../core/key_normalizer.hpp"

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <vector>

namespace keystats {
namespace {

constexpr UINT kRidInput = 0x10000003;
constexpr DWORD kRidevRemove = 0x00000001;
constexpr DWORD kRidevInputSink = 0x00000100;
constexpr DWORD kRidevDevNotify = 0x00002000;
constexpr int kGidcRemoval = 2;
constexpr int kVkMenu = 0x12;
constexpr int kVkTab = 0x09;
constexpr USHORT kRiMouseLeftButtonDown = 0x0001;
constexpr USHORT kRiMouseLeftButtonUp = 0x0002;
constexpr USHORT kRiMouseRightButtonDown = 0x0004;
constexpr USHORT kRiMouseRightButtonUp = 0x0008;
constexpr UINT kAltSampleTimer = 42;

std::string FormatHex(unsigned value, int width) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%0*X", width, value);
    return buffer;
}

}  // namespace

RawInputSource::RawInputSource(HWND window, KeyboardCounter& counter, std::function<void(KeyId)> counted_sink)
    : window_(window), counter_(counter), counted_sink_(std::move(counted_sink)) {
    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x02;
    devices[0].dwFlags = kRidevInputSink | kRidevDevNotify;
    devices[0].hwndTarget = window;
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x06;
    devices[1].dwFlags = kRidevInputSink | kRidevDevNotify;
    devices[1].hwndTarget = window;
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
        throw std::runtime_error("注册键盘和鼠标 Raw Input 失败。");
    }
}

RawInputSource::~RawInputSource() {
    disposed_ = true;
    KillTimer(window_, kAltSampleTimer);
    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x02;
    devices[0].dwFlags = kRidevRemove;
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x06;
    devices[1].dwFlags = kRidevRemove;
    RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
}

bool RawInputSource::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_INPUT) {
        ProcessRawInput(l_param);
        return false;
    }
    if (message == WM_INPUT_DEVICE_CHANGE && w_param == kGidcRemoval) {
        counter_.RemoveDevice(static_cast<std::intptr_t>(l_param));
        return false;
    }
    if (message == WM_TIMER && w_param == kAltSampleTimer) {
        OnAltSamplingTick();
        return true;
    }
    return false;
}

void RawInputSource::ReconcileSystemShortcutState() {
    ReleaseIfPhysicallyUp(KeyId::Tab, 0x09);
    ReleaseIfPhysicallyUp(KeyId::LeftAlt, 0xA4);
    ReleaseIfPhysicallyUp(KeyId::RightAlt, 0xA5);
}

void RawInputSource::ClearDiagnostics() {
    unrecognized_make_count_ = 0;
    last_input_description_ = "等待键盘输入…";
    last_unrecognized_description_.clear();
}

void RawInputSource::OnAltSamplingTick() {
    if (disposed_) {
        return;
    }
    const bool alt_down = (GetAsyncKeyState(kVkMenu) & 0x8000) != 0;
    const bool tab_down = (GetAsyncKeyState(kVkTab) & 0x8000) != 0;
    if (!alt_down) {
        if (sampled_alt_tab_down_) {
            sampled_alt_tab_down_ = false;
            counter_.ProcessButton(0, KeyId::Tab, true);
        }
        KillTimer(window_, kAltSampleTimer);
        return;
    }
    const bool alt_tab_down = alt_down && tab_down;
    if (alt_tab_down == sampled_alt_tab_down_) {
        return;
    }
    sampled_alt_tab_down_ = alt_tab_down;
    const auto result = counter_.ProcessButton(0, KeyId::Tab, !alt_tab_down);
    if (result.counted) {
        if (counted_sink_) {
            counted_sink_(KeyId::Tab);
        }
        last_input_description_ = "最近识别：Tab（Alt+Tab 系统切换补偿）";
    }
}

void RawInputSource::UpdateSystemShortcutSampling(const std::optional<KeyId>& key_id, bool is_break) {
    if (!key_id || (*key_id != KeyId::LeftAlt && *key_id != KeyId::RightAlt)) {
        return;
    }
    if (!is_break) {
        SetTimer(window_, kAltSampleTimer, 15, nullptr);
        return;
    }
    if ((GetAsyncKeyState(kVkMenu) & 0x8000) != 0) {
        return;
    }
    KillTimer(window_, kAltSampleTimer);
    if (sampled_alt_tab_down_) {
        sampled_alt_tab_down_ = false;
        counter_.ProcessButton(0, KeyId::Tab, true);
    }
}

void RawInputSource::ReleaseIfPhysicallyUp(KeyId key_id, int virtual_key) {
    if ((GetAsyncKeyState(virtual_key) & 0x8000) == 0) {
        counter_.ReleaseKey(key_id);
    }
}

void RawInputSource::ProcessRawInput(LPARAM raw_input_handle) {
    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(raw_input_handle), kRidInput, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) {
        return;
    }
    std::vector<std::uint8_t> buffer(size);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(raw_input_handle), kRidInput, buffer.data(), &size,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }
    const auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
    const auto device = static_cast<std::intptr_t>(reinterpret_cast<std::intptr_t>(raw->header.hDevice));
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const auto& keyboard = raw->data.keyboard;
        if (keyboard.MakeCode == 0xFF || keyboard.VKey >= 0xFF) {
            return;
        }
        RawKeyEvent key_event{keyboard.MakeCode, static_cast<RawKeyFlags>(keyboard.Flags), keyboard.VKey};
        const auto process_result = counter_.ProcessDetailed(device, key_event);
        UpdateSystemShortcutSampling(process_result.key_id, key_event.IsBreak());
        if (process_result.counted && process_result.key_id) {
            if (counted_sink_) {
                counted_sink_(*process_result.key_id);
            }
        }
        if (!key_event.IsBreak()) {
            const auto normalized = KeyNormalizer::Normalize(key_event);
            if (normalized) {
                last_input_description_ = "最近识别：" + std::string(KeyCatalog::Get(*normalized).display_name) +
                                          "  ·  Scan " + FormatHex(keyboard.MakeCode, 2) + "  ·  VK " +
                                          FormatHex(keyboard.VKey, 2);
            } else {
                last_input_description_ = "最近未识别：Scan " + FormatHex(keyboard.MakeCode, 2) + "  ·  VK " +
                                          FormatHex(keyboard.VKey, 2) + "  ·  Flags " + FormatHex(keyboard.Flags, 2);
            }
            if (!process_result.recognized) {
                ++unrecognized_make_count_;
                last_unrecognized_description_ = "Scan " + FormatHex(keyboard.MakeCode, 2) + " · VK " +
                                                 FormatHex(keyboard.VKey, 2) + " · Flags " + FormatHex(keyboard.Flags, 2);
            }
        }
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        const auto flags = raw->data.mouse.usButtonFlags;
        auto handle_button = [&](USHORT down, USHORT up, KeyId id) {
            if (flags & down) {
                const auto result = counter_.ProcessButton(device, id, false);
                if (result.counted && counted_sink_) {
                    counted_sink_(id);
                }
                last_input_description_ = "最近识别：" + std::string(KeyCatalog::Get(id).display_name);
            }
            if (flags & up) {
                counter_.ProcessButton(device, id, true);
            }
        };
        handle_button(kRiMouseLeftButtonDown, kRiMouseLeftButtonUp, KeyId::MouseLeftButton);
        handle_button(kRiMouseRightButtonDown, kRiMouseRightButtonUp, KeyId::MouseRightButton);
    }
}

}  // namespace keystats
