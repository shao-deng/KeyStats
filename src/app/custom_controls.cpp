#include "custom_controls.hpp"

#include "../core/key_catalog.hpp"
#include "utf.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_set>

namespace keystats {
namespace {

constexpr COLORREF kEmpty = RGB(247, 248, 250);
constexpr COLORREF kLow = RGB(218, 235, 249);
constexpr COLORREF kMid = RGB(91, 151, 211);
constexpr COLORREF kHigh = RGB(235, 139, 57);
constexpr COLORREF kBorder = RGB(206, 214, 223);
constexpr COLORREF kPressedBorder = RGB(33, 103, 184);

BYTE Mix(BYTE a, BYTE b, double t) {
    return static_cast<BYTE>(std::lround(a + (b - a) * t));
}

COLORREF Interpolate(COLORREF start, COLORREF end, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    return RGB(Mix(GetRValue(start), GetRValue(end), amount), Mix(GetGValue(start), GetGValue(end), amount),
               Mix(GetBValue(start), GetBValue(end), amount));
}

COLORREF HeatColor(std::uint64_t count, double intensity) {
    if (count == 0) {
        return kEmpty;
    }
    return intensity <= 0.58 ? Interpolate(kLow, kMid, intensity / 0.58)
                             : Interpolate(kMid, kHigh, (intensity - 0.58) / 0.42);
}

struct KeyCap {
    KeyId id;
    const wchar_t* label;
    float x;
    float y;
    float w;
    float h;
};

constexpr KeyCap kKeys[] = {
    {KeyId::Escape, L"Esc", 0, 0, 1, 1},
    {KeyId::F1, L"F1", 2, 0, 1, 1}, {KeyId::F2, L"F2", 3, 0, 1, 1}, {KeyId::F3, L"F3", 4, 0, 1, 1},
    {KeyId::F4, L"F4", 5, 0, 1, 1}, {KeyId::F5, L"F5", 6.5f, 0, 1, 1}, {KeyId::F6, L"F6", 7.5f, 0, 1, 1},
    {KeyId::F7, L"F7", 8.5f, 0, 1, 1}, {KeyId::F8, L"F8", 9.5f, 0, 1, 1}, {KeyId::F9, L"F9", 11, 0, 1, 1},
    {KeyId::F10, L"F10", 12, 0, 1, 1}, {KeyId::F11, L"F11", 13, 0, 1, 1}, {KeyId::F12, L"F12", 14, 0, 1, 1},
    {KeyId::PrintScreen, L"PrtSc", 15.5f, 0, 1, 1}, {KeyId::ScrollLock, L"ScrLk", 16.5f, 0, 1, 1},
    {KeyId::Pause, L"Pause", 17.5f, 0, 1, 1},
    {KeyId::MouseLeftButton, L"鼠标左键", 20, 0, 1.7f, 1}, {KeyId::MouseRightButton, L"鼠标右键", 21.7f, 0, 1.7f, 1},

    {KeyId::Grave, L"`", 0, 1, 1, 1}, {KeyId::Digit1, L"1", 1, 1, 1, 1}, {KeyId::Digit2, L"2", 2, 1, 1, 1},
    {KeyId::Digit3, L"3", 3, 1, 1, 1}, {KeyId::Digit4, L"4", 4, 1, 1, 1}, {KeyId::Digit5, L"5", 5, 1, 1, 1},
    {KeyId::Digit6, L"6", 6, 1, 1, 1}, {KeyId::Digit7, L"7", 7, 1, 1, 1}, {KeyId::Digit8, L"8", 8, 1, 1, 1},
    {KeyId::Digit9, L"9", 9, 1, 1, 1}, {KeyId::Digit0, L"0", 10, 1, 1, 1}, {KeyId::Minus, L"-", 11, 1, 1, 1},
    {KeyId::Equal, L"=", 12, 1, 1, 1}, {KeyId::Backspace, L"Backspace", 13, 1, 2, 1},
    {KeyId::Insert, L"Ins", 16, 1, 1, 1}, {KeyId::Home, L"Home", 17, 1, 1, 1}, {KeyId::PageUp, L"PgUp", 18, 1, 1, 1},
    {KeyId::NumLock, L"Num", 20, 1, 1, 1}, {KeyId::NumpadDivide, L"/", 21, 1, 1, 1},
    {KeyId::NumpadMultiply, L"*", 22, 1, 1, 1}, {KeyId::NumpadSubtract, L"-", 23, 1, 1, 1},

    {KeyId::Tab, L"Tab", 0, 2, 1.5f, 1}, {KeyId::Q, L"Q", 1.5f, 2, 1, 1}, {KeyId::W, L"W", 2.5f, 2, 1, 1},
    {KeyId::E, L"E", 3.5f, 2, 1, 1}, {KeyId::R, L"R", 4.5f, 2, 1, 1}, {KeyId::T, L"T", 5.5f, 2, 1, 1},
    {KeyId::Y, L"Y", 6.5f, 2, 1, 1}, {KeyId::U, L"U", 7.5f, 2, 1, 1}, {KeyId::I, L"I", 8.5f, 2, 1, 1},
    {KeyId::O, L"O", 9.5f, 2, 1, 1}, {KeyId::P, L"P", 10.5f, 2, 1, 1}, {KeyId::LeftBracket, L"[", 11.5f, 2, 1, 1},
    {KeyId::RightBracket, L"]", 12.5f, 2, 1, 1}, {KeyId::Backslash, L"\\", 13.5f, 2, 1.5f, 1},
    {KeyId::Delete, L"Del", 16, 2, 1, 1}, {KeyId::End, L"End", 17, 2, 1, 1}, {KeyId::PageDown, L"PgDn", 18, 2, 1, 1},
    {KeyId::Numpad7, L"7", 20, 2, 1, 1}, {KeyId::Numpad8, L"8", 21, 2, 1, 1}, {KeyId::Numpad9, L"9", 22, 2, 1, 1},
    {KeyId::NumpadAdd, L"+", 23, 2, 1, 2},

    {KeyId::CapsLock, L"Caps", 0, 3, 1.75f, 1}, {KeyId::A, L"A", 1.75f, 3, 1, 1}, {KeyId::S, L"S", 2.75f, 3, 1, 1},
    {KeyId::D, L"D", 3.75f, 3, 1, 1}, {KeyId::F, L"F", 4.75f, 3, 1, 1}, {KeyId::G, L"G", 5.75f, 3, 1, 1},
    {KeyId::H, L"H", 6.75f, 3, 1, 1}, {KeyId::J, L"J", 7.75f, 3, 1, 1}, {KeyId::K, L"K", 8.75f, 3, 1, 1},
    {KeyId::L, L"L", 9.75f, 3, 1, 1}, {KeyId::Semicolon, L";", 10.75f, 3, 1, 1},
    {KeyId::Apostrophe, L"'", 11.75f, 3, 1, 1}, {KeyId::Enter, L"Enter", 12.75f, 3, 2.25f, 1},
    {KeyId::Numpad4, L"4", 20, 3, 1, 1}, {KeyId::Numpad5, L"5", 21, 3, 1, 1}, {KeyId::Numpad6, L"6", 22, 3, 1, 1},

    {KeyId::LeftShift, L"Shift", 0, 4, 2.25f, 1}, {KeyId::Z, L"Z", 2.25f, 4, 1, 1}, {KeyId::X, L"X", 3.25f, 4, 1, 1},
    {KeyId::C, L"C", 4.25f, 4, 1, 1}, {KeyId::V, L"V", 5.25f, 4, 1, 1}, {KeyId::B, L"B", 6.25f, 4, 1, 1},
    {KeyId::N, L"N", 7.25f, 4, 1, 1}, {KeyId::M, L"M", 8.25f, 4, 1, 1}, {KeyId::Comma, L",", 9.25f, 4, 1, 1},
    {KeyId::Period, L".", 10.25f, 4, 1, 1}, {KeyId::Slash, L"/", 11.25f, 4, 1, 1},
    {KeyId::RightShift, L"Shift", 12.25f, 4, 2.75f, 1}, {KeyId::ArrowUp, L"↑", 17, 4, 1, 1},
    {KeyId::Numpad1, L"1", 20, 4, 1, 1}, {KeyId::Numpad2, L"2", 21, 4, 1, 1}, {KeyId::Numpad3, L"3", 22, 4, 1, 1},
    {KeyId::NumpadEnter, L"Enter", 23, 4, 1, 2},

    {KeyId::LeftControl, L"Ctrl", 0, 5, 1.25f, 1}, {KeyId::LeftWindows, L"Win", 1.25f, 5, 1.25f, 1},
    {KeyId::LeftAlt, L"Alt", 2.5f, 5, 1.25f, 1}, {KeyId::Space, L"Space", 3.75f, 5, 6.25f, 1},
    {KeyId::RightAlt, L"Alt", 10, 5, 1.25f, 1}, {KeyId::RightWindows, L"Win", 11.25f, 5, 1.25f, 1},
    {KeyId::Application, L"Menu", 12.5f, 5, 1.25f, 1}, {KeyId::RightControl, L"Ctrl", 13.75f, 5, 1.25f, 1},
    {KeyId::ArrowLeft, L"←", 16, 5, 1, 1}, {KeyId::ArrowDown, L"↓", 17, 5, 1, 1}, {KeyId::ArrowRight, L"→", 18, 5, 1, 1},
    {KeyId::Numpad0, L"0", 20, 5, 2, 1}, {KeyId::NumpadDecimal, L".", 22, 5, 1, 1},
};

struct HeatmapState {
    std::array<std::uint64_t, kKeyCount> counts{};
    std::unordered_set<int> pressed;
    HeatScaleMode mode = HeatScaleMode::SquareRoot;
};

struct TimelineState {
    std::array<std::uint64_t, 144> counts{};
};

HeatmapState* Heat(HWND hwnd) { return reinterpret_cast<HeatmapState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }
TimelineState* Time(HWND hwnd) { return reinterpret_cast<TimelineState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

void DrawRoundRect(HDC dc, RECT rect, COLORREF fill, COLORREF border, int thickness) {
    HPEN pen = CreatePen(PS_SOLID, thickness, border);
    HBRUSH brush = CreateSolidBrush(fill);
    auto old_pen = SelectObject(dc, pen);
    auto old_brush = SelectObject(dc, brush);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 10, 10);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

LRESULT CALLBACK HeatmapProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_CREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new HeatmapState()));
        return 0;
    }
    if (message == WM_DESTROY) {
        delete Heat(hwnd);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        auto* state = Heat(hwnd);
        if (!state) {
            EndPaint(hwnd, &paint);
            return 0;
        }
        std::uint64_t maximum = 0;
        std::uint64_t total = 0;
        for (const auto& cap : kKeys) {
            maximum = std::max(maximum, state->counts[ToIndex(cap.id)]);
        }
        for (const auto count : state->counts) {
            total += count;
        }
        const double unit = std::min((client.right - 8) / (24.0 * 52.0 / 48.0), (client.bottom - 8) / 6.2);
        const double gap = unit * 4.0 / 48.0;
        const double step = unit + gap;
        HFONT font = CreateFontW(-static_cast<int>(unit * 0.22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HFONT count_font = CreateFontW(-static_cast<int>(unit * 0.24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SetBkMode(dc, TRANSPARENT);
        for (const auto& cap : kKeys) {
            RECT rect{
                static_cast<LONG>(8 + cap.x * step),
                static_cast<LONG>(4 + cap.y * step),
                static_cast<LONG>(8 + (cap.x + cap.w) * step - gap),
                static_cast<LONG>(4 + (cap.y + cap.h) * step - gap),
            };
            const auto count = state->counts[ToIndex(cap.id)];
            double intensity = maximum == 0 ? 0.0 : static_cast<double>(count) / static_cast<double>(maximum);
            if (state->mode == HeatScaleMode::SquareRoot) {
                intensity = std::sqrt(intensity);
            }
            const bool pressed = state->pressed.contains(ToIndex(cap.id));
            DrawRoundRect(dc, rect, HeatColor(count, intensity), pressed ? kPressedBorder : kBorder, pressed ? 3 : 1);
            SetTextColor(dc, intensity >= 0.72 ? RGB(255, 248, 238) : RGB(80, 90, 102));
            auto old = SelectObject(dc, font);
            RECT name = rect;
            name.bottom -= (rect.bottom - rect.top) / 2;
            DrawTextW(dc, cap.label, -1, &name, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, count_font);
            SetTextColor(dc, intensity >= 0.72 ? RGB(255, 255, 255) : RGB(35, 43, 52));
            RECT number = rect;
            number.top += (rect.bottom - rect.top) / 2;
            auto text = std::to_wstring(count);
            DrawTextW(dc, text.c_str(), -1, &number, DT_CENTER | DT_TOP | DT_SINGLELINE);
            SelectObject(dc, old);
        }
        DeleteObject(font);
        DeleteObject(count_font);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_HOVER, hwnd, 400};
        TrackMouseEvent(&track);
        return 0;
    }
    if (message == WM_MOUSEHOVER) {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        RECT client{};
        GetClientRect(hwnd, &client);
        const double unit = std::min((client.right - 8) / (24.0 * 52.0 / 48.0), (client.bottom - 8) / 6.2);
        const double gap = unit * 4.0 / 48.0;
        const double step = unit + gap;
        auto* state = Heat(hwnd);
        for (const auto& cap : kKeys) {
            RECT rect{
                static_cast<LONG>(8 + cap.x * step),
                static_cast<LONG>(4 + cap.y * step),
                static_cast<LONG>(8 + (cap.x + cap.w) * step - gap),
                static_cast<LONG>(4 + (cap.y + cap.h) * step - gap),
            };
            if (PtInRect(&rect, point) && state) {
                const auto count = state->counts[ToIndex(cap.id)];
                std::uint64_t total = 0;
                for (auto value : state->counts) total += value;
                const double percent = total == 0 ? 0.0 : count * 100.0 / total;
                wchar_t tip[256];
                swprintf_s(tip, L"%s\n次数：%llu\n占全部输入：%.2f%%", cap.label,
                           static_cast<unsigned long long>(count), percent);
                SetWindowTextW(hwnd, tip);
                // Use a simple tooltip via title; also Message-less balloon via TrackPopup is overkill.
                break;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
}

std::wstring FormatHm(int minutes) {
    if (minutes == 24 * 60) {
        return L"24:00";
    }
    wchar_t buffer[16];
    swprintf_s(buffer, L"%02d:%02d", minutes / 60, minutes % 60);
    return buffer;
}

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_CREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new TimelineState()));
        return 0;
    }
    if (message == WM_DESTROY) {
        delete Time(hwnd);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        auto* state = Time(hwnd);
        std::uint64_t maximum = 0;
        if (state) {
            maximum = *std::max_element(state->counts.begin(), state->counts.end());
        }
        const int bar_height = 18;
        const double width = client.right / 144.0;
        for (int index = 0; index < 144; ++index) {
            const auto count = state ? state->counts[static_cast<std::size_t>(index)] : 0;
            const double intensity = maximum == 0 ? 0.0 : std::sqrt(static_cast<double>(count) / maximum);
            RECT rect{static_cast<LONG>(index * width), 0, static_cast<LONG>((index + 1) * width), bar_height};
            HBRUSH brush = CreateSolidBrush(count == 0 ? RGB(239, 242, 246) : HeatColor(count, intensity));
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
            if (index % 6 == 0) {
                HPEN pen = CreatePen(PS_SOLID, 1, RGB(167, 181, 196));
                auto old = SelectObject(dc, pen);
                MoveToEx(dc, rect.left, 0, nullptr);
                LineTo(dc, rect.left, bar_height);
                SelectObject(dc, old);
                DeleteObject(pen);
            }
        }
        HFONT font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                 0, L"Segoe UI");
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(105, 113, 122));
        for (int hour = 0; hour < 24; hour += 2) {
            RECT label{static_cast<LONG>(hour * 6 * width), bar_height, static_cast<LONG>((hour * 6 + 12) * width),
                       client.bottom};
            auto text = FormatHm(hour * 60);
            DrawTextW(dc, text.c_str(), -1, &label, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
        SelectObject(dc, old);
        DeleteObject(font);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
}

}  // namespace

void RegisterCustomControls(HINSTANCE instance) {
    WNDCLASSW heat{};
    heat.lpfnWndProc = HeatmapProc;
    heat.hInstance = instance;
    heat.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    heat.lpszClassName = L"KeyStatsHeatmap";
    heat.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    RegisterClassW(&heat);
    WNDCLASSW time{};
    time.lpfnWndProc = TimelineProc;
    time.hInstance = instance;
    time.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    time.lpszClassName = L"KeyStatsTimeline";
    time.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    RegisterClassW(&time);
}

HWND CreateHeatmapControl(HWND parent, int control_id, RECT bounds) {
    return CreateWindowExW(0, L"KeyStatsHeatmap", L"", WS_CHILD | WS_VISIBLE, bounds.left, bounds.top,
                           bounds.right - bounds.left, bounds.bottom - bounds.top, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), GetModuleHandleW(nullptr), nullptr);
}

HWND CreateTimelineControl(HWND parent, int control_id, RECT bounds) {
    return CreateWindowExW(0, L"KeyStatsTimeline", L"", WS_CHILD | WS_VISIBLE, bounds.left, bounds.top,
                           bounds.right - bounds.left, bounds.bottom - bounds.top, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), GetModuleHandleW(nullptr), nullptr);
}

void SetHeatmapData(HWND heatmap, const std::array<std::uint64_t, kKeyCount>& counts,
                    const std::unordered_set<int>& pressed, HeatScaleMode mode) {
    if (auto* state = Heat(heatmap)) {
        if (state->counts == counts && state->pressed == pressed && state->mode == mode) {
            return;
        }
        state->counts = counts;
        state->pressed = pressed;
        state->mode = mode;
        InvalidateRect(heatmap, nullptr, FALSE);
    }
}

void SetTimelineData(HWND timeline, const std::array<std::uint64_t, 144>& counts) {
    if (auto* state = Time(timeline)) {
        if (state->counts == counts) {
            return;
        }
        state->counts = counts;
        InvalidateRect(timeline, nullptr, FALSE);
    }
}

std::wstring TimelineHitTooltip(HWND timeline, int x) {
    RECT client{};
    GetClientRect(timeline, &client);
    const double width = client.right / 144.0;
    int index = static_cast<int>(x / width);
    index = std::clamp(index, 0, 143);
    auto* state = Time(timeline);
    const auto count = state ? state->counts[static_cast<std::size_t>(index)] : 0;
    return FormatHm(index * 10) + L"— " + FormatHm((index + 1) * 10) + L"\n输入次数：" + std::to_wstring(count);
}

}  // namespace keystats
