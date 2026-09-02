#include "application.hpp"

#include "custom_controls.hpp"
#include "resource.h"
#include "raw_input.hpp"
#include "startup.hpp"
#include "tray.hpp"
#include "utf.hpp"

#include "../core/key_catalog.hpp"
#include "../core/keyboard_counter.hpp"
#include "../storage/repository.hpp"
#include "../storage/settings_store.hpp"
#include "../storage/statistics_recorder.hpp"
#include "../storage/time_buckets.hpp"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace keystats {
namespace {

constexpr int kIdStartup = 1001;
constexpr int kIdStatus = 1002;
constexpr int kIdRange = 1003;
constexpr int kIdStartDate = 1004;
constexpr int kIdStartTime = 1005;
constexpr int kIdEndDate = 1006;
constexpr int kIdEndTime = 1007;
constexpr int kIdSnap = 1008;
constexpr int kIdApply = 1009;
constexpr int kIdScale = 1010;
constexpr int kIdTimeline = 1011;
constexpr int kIdTimelineSummary = 1012;
constexpr int kIdTotal = 1013;
constexpr int kIdHistory = 1014;
constexpr int kIdPause = 1015;
constexpr int kIdClear = 1016;
constexpr int kIdSave = 1017;
constexpr int kIdExport = 1018;
constexpr int kIdPressed = 1019;
constexpr int kIdLastInput = 1020;
constexpr int kIdUnknown = 1021;
constexpr int kIdStorage = 1022;
constexpr int kIdTabs = 1023;
constexpr int kIdHeatmap = 1024;
constexpr int kIdList = 1025;
constexpr int kIdActiveOnly = 1026;
constexpr int kIdHeatRange = 1027;
constexpr int kIdHeatTotal = 1028;
constexpr int kIdHeatUpdated = 1029;
constexpr int kIdRangeError = 1030;
constexpr int kRefreshTimer = 1;

HWND g_window = nullptr;
HWND g_heatmap = nullptr;
HWND g_timeline = nullptr;
HWND g_list = nullptr;
HWND g_tabs = nullptr;
KeyboardCounter g_counter;
std::unique_ptr<RawInputSource> g_raw;
std::unique_ptr<TrayIcon> g_tray;
std::unique_ptr<StartupRegistration> g_startup;
std::unique_ptr<KeyStatsRepository> g_repository;
std::unique_ptr<StatisticsRecorder> g_recorder;
std::string g_range_preset = "Today";
std::optional<std::pair<long long, long long>> g_selected_range;
std::array<std::uint64_t, kKeyCount> g_selected_counts{};
std::array<std::uint64_t, kKeyCount> g_selected_stored_counts{};
std::array<std::uint64_t, 144> g_selected_stored_time{};
std::optional<long long> g_live_bucket;
std::optional<std::array<std::uint32_t, kKeyCount>> g_last_live_counts;
bool g_closing = false;
bool g_startup_changing = false;
HFONT g_font = nullptr;
HFONT g_title_font = nullptr;
long long g_last_storage_refresh = 0;

std::wstring FormatInt(std::uint64_t value) {
    std::wstring text = std::to_wstring(value);
    std::wstring grouped;
    int count = 0;
    for (int i = static_cast<int>(text.size()) - 1; i >= 0; --i) {
        grouped.push_back(text[static_cast<std::size_t>(i)]);
        if (++count == 3 && i != 0) {
            grouped.push_back(L',');
            count = 0;
        }
    }
    std::reverse(grouped.begin(), grouped.end());
    return grouped;
}

long long NowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::tm UnixToLocal(long long unix_seconds) {
    const std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
    localtime_s(&local, &time);
    return local;
}

long long LocalToUnix(const std::tm& local) {
    std::tm copy = local;
    return static_cast<long long>(_mkgmtime(&copy) == -1 ? 0 : std::mktime(&copy));
}

std::pair<long long, long long> GetTodayRangeUtc() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    std::tm start{};
    start.tm_year = local.wYear - 1900;
    start.tm_mon = local.wMonth - 1;
    start.tm_mday = local.wDay;
    start.tm_isdst = -1;
    const auto start_utc = ToMinuteStartUnix(LocalToUnix(start));
    const auto current = ToMinuteStartUnix(NowUnix());
    return {start_utc, current + kMinuteSeconds};
}

std::pair<long long, long long> ResolvePreset(const std::string& preset) {
    const auto current = ToMinuteStartUnix(NowUnix());
    const auto end_utc = current + kMinuteSeconds;
    if (preset == "Recent10") {
        return {end_utc - kTenMinuteSeconds, end_utc};
    }
    if (preset == "Recent7Days") {
        return {end_utc - 7LL * 24 * 60 * kMinuteSeconds, end_utc};
    }
    if (preset == "Recent30Days") {
        return {end_utc - 30LL * 24 * 60 * kMinuteSeconds, end_utc};
    }
    if (preset == "All") {
        if (g_repository) {
            if (const auto stored = g_repository->GetDataRange()) {
                return {std::min(stored->start_utc, current), std::max(stored->end_utc, end_utc)};
            }
        }
        return {current, end_utc};
    }
    return GetTodayRangeUtc();
}

void SetRangeEditors(const std::pair<long long, long long>& range) {
    const auto start = UnixToLocal(range.first);
    const auto end = UnixToLocal(range.second);
    SYSTEMTIME start_st{};
    start_st.wYear = static_cast<WORD>(start.tm_year + 1900);
    start_st.wMonth = static_cast<WORD>(start.tm_mon + 1);
    start_st.wDay = static_cast<WORD>(start.tm_mday);
    DateTime_SetSystemtime(GetDlgItem(g_window, kIdStartDate), GDT_VALID, &start_st);
    SYSTEMTIME end_st = start_st;
    end_st.wYear = static_cast<WORD>(end.tm_year + 1900);
    end_st.wMonth = static_cast<WORD>(end.tm_mon + 1);
    end_st.wDay = static_cast<WORD>(end.tm_mday);
    DateTime_SetSystemtime(GetDlgItem(g_window, kIdEndDate), GDT_VALID, &end_st);
    wchar_t start_time[8];
    swprintf_s(start_time, L"%02d:%02d", start.tm_hour, start.tm_min);
    SetWindowTextW(GetDlgItem(g_window, kIdStartTime), start_time);
    wchar_t end_time[8];
    swprintf_s(end_time, L"%02d:%02d", end.tm_hour, end.tm_min);
    SetWindowTextW(GetDlgItem(g_window, kIdEndTime), end_time);
}

void EnableCustomEditors(bool enabled) {
    EnableWindow(GetDlgItem(g_window, kIdStartDate), enabled);
    EnableWindow(GetDlgItem(g_window, kIdStartTime), enabled);
    EnableWindow(GetDlgItem(g_window, kIdEndDate), enabled);
    EnableWindow(GetDlgItem(g_window, kIdEndTime), enabled);
    EnableWindow(GetDlgItem(g_window, kIdSnap), enabled);
    EnableWindow(GetDlgItem(g_window, kIdApply), enabled);
}

HeatScaleMode CurrentScale() {
    const auto index = SendMessageW(GetDlgItem(g_window, kIdScale), CB_GETCURSEL, 0, 0);
    return index == 1 ? HeatScaleMode::Linear : HeatScaleMode::SquareRoot;
}

void RebuildList(const KeyboardSnapshot& snapshot) {
    const bool active_only = SendMessageW(GetDlgItem(g_window, kIdActiveOnly), BM_GETCHECK, 0, 0) == BST_CHECKED;
    ListView_DeleteAllItems(g_list);
    int row = 0;
    for (const auto& definition : KeyCatalog::Definitions()) {
        const auto count = g_selected_counts[static_cast<std::size_t>(definition.display_order)];
        const bool pressed = snapshot.IsPressed(definition.id);
        if (active_only && count == 0 && !pressed) {
            continue;
        }
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        auto name = Utf8ToWide(definition.display_name);
        item.pszText = name.data();
        ListView_InsertItem(g_list, &item);
        auto category = Utf8ToWide(definition.category);
        ListView_SetItemText(g_list, row, 1, category.data());
        auto count_text = FormatInt(count);
        ListView_SetItemText(g_list, row, 2, count_text.data());
        wchar_t status[] = L"按下中";
        ListView_SetItemText(g_list, row, 3, pressed ? status : const_cast<LPWSTR>(L""));
        auto id = Utf8ToWide(std::string(KeyIdName(definition.id)));
        ListView_SetItemText(g_list, row, 4, id.data());
        ++row;
    }
}

void ApplySelectedCounts(const std::array<std::uint64_t, kKeyCount>& counts,
                         const std::array<std::uint64_t, 144>& time_counts, const KeyboardSnapshot& snapshot) {
    g_selected_counts = counts;
    const auto total = std::accumulate(counts.begin(), counts.end(), 0ULL);
    SetWindowTextW(GetDlgItem(g_window, kIdHeatTotal), (L"所选范围总计：" + FormatInt(total)).c_str());
    SetHeatmapData(g_heatmap, counts, snapshot.pressed_keys, CurrentScale());
    SetTimelineData(g_timeline, time_counts);
    std::uint64_t peak = 0;
    int peak_index = 0;
    for (int i = 0; i < 144; ++i) {
        if (time_counts[static_cast<std::size_t>(i)] > peak) {
            peak = time_counts[static_cast<std::size_t>(i)];
            peak_index = i;
        }
    }
    if (peak == 0) {
        SetWindowTextW(GetDlgItem(g_window, kIdTimelineSummary), L"暂无输入");
    } else {
        wchar_t text[128];
        swprintf_s(text, L"峰值 %02d:%02d—%02d:%02d · %s 次", (peak_index * 10) / 60, (peak_index * 10) % 60,
                   ((peak_index + 1) * 10) / 60, ((peak_index + 1) * 10) % 60, FormatInt(peak).c_str());
        SetWindowTextW(GetDlgItem(g_window, kIdTimelineSummary), text);
    }
    RebuildList(snapshot);
}

void AddLiveTime(std::array<std::uint64_t, 144>& destination, const MinuteBucketSnapshot& snapshot) {
    const auto local = UnixToLocal(snapshot.bucket_start_utc);
    const int index = local.tm_hour * 6 + local.tm_min / 10;
    destination[static_cast<std::size_t>(index)] += static_cast<std::uint64_t>(snapshot.TotalCount());
}

void RefreshSelectedRange(bool force) {
    if (!g_repository || !g_selected_range) {
        return;
    }
    static bool running = false;
    static bool loaded = false;
    if (running || (!force && loaded)) {
        return;
    }
    running = true;
    try {
        const auto range = *g_selected_range;
        std::array<std::uint64_t, kKeyCount> stored{};
        std::array<std::uint64_t, 144> stored_time{};
        auto live = g_recorder ? std::optional(g_recorder->GetCurrentSnapshot()) : std::nullopt;
        auto accumulate = [&](long long from, long long to) {
            if (from >= to) return;
            const auto aggregate = g_repository->QueryAggregate(from, to);
            for (int i = 0; i < kKeyCount; ++i) stored[static_cast<std::size_t>(i)] += aggregate.counts[static_cast<std::size_t>(i)];
            for (const auto& bucket : g_repository->QueryTenMinuteTotals(from, to)) {
                const auto local = UnixToLocal(bucket.bucket_start_utc);
                stored_time[static_cast<std::size_t>(local.tm_hour * 6 + local.tm_min / 10)] += bucket.total_count;
            }
        };
        if (live && live->bucket_start_utc >= range.first && live->bucket_start_utc < range.second) {
            accumulate(range.first, live->bucket_start_utc);
            accumulate(live->bucket_start_utc + kMinuteSeconds, range.second);
            g_live_bucket = live->bucket_start_utc;
            g_last_live_counts = live->counts;
        } else {
            accumulate(range.first, range.second);
            g_live_bucket.reset();
            g_last_live_counts.reset();
        }
        g_selected_stored_counts = stored;
        g_selected_stored_time = stored_time;
        auto display = stored;
        auto display_time = stored_time;
        if (live && g_live_bucket == live->bucket_start_utc) {
            for (int i = 0; i < kKeyCount; ++i) {
                display[static_cast<std::size_t>(i)] += live->counts[static_cast<std::size_t>(i)];
            }
            AddLiveTime(display_time, *live);
        }
        ApplySelectedCounts(display, display_time, g_counter.GetSnapshot());
        const auto start = UnixToLocal(range.first);
        const auto end = UnixToLocal(range.second);
        wchar_t text[128];
        swprintf_s(text, L"%04d-%02d-%02d %02d:%02d — %04d-%02d-%02d %02d:%02d（结束不含）", start.tm_year + 1900,
                   start.tm_mon + 1, start.tm_mday, start.tm_hour, start.tm_min, end.tm_year + 1900, end.tm_mon + 1,
                   end.tm_mday, end.tm_hour, end.tm_min);
        SetWindowTextW(GetDlgItem(g_window, kIdHeatRange), text);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t updated[64];
        swprintf_s(updated, L"更新于 %02d:%02d:%02d", now.wHour, now.wMinute, now.wSecond);
        SetWindowTextW(GetDlgItem(g_window, kIdHeatUpdated), updated);
        loaded = true;
    } catch (const std::exception& exception) {
        SetWindowTextUtf8(GetDlgItem(g_window, kIdRangeError), std::string("查询失败：") + exception.what());
    }
    running = false;
}

void RefreshStorageUi(bool force) {
    if (!g_repository) {
        return;
    }
    const auto now = NowUnix();
    if (!force && now - g_last_storage_refresh < 2) {
        return;
    }
    try {
        auto live = g_recorder ? std::optional(g_recorder->GetCurrentSnapshot()) : std::nullopt;
        auto stored_total = g_repository->QueryTotalCount(live ? std::optional(live->bucket_start_utc) : std::nullopt);
        const auto total = stored_total + static_cast<std::uint64_t>(live ? live->TotalCount() : 0);
        SetWindowTextW(GetDlgItem(g_window, kIdHistory), (L"总数量：" + FormatInt(total)).c_str());
        std::wstring saved = L"尚未保存";
        if (g_recorder) {
            if (const auto stamp = g_recorder->LastSavedUnixSeconds()) {
                const auto local = UnixToLocal(*stamp);
                wchar_t buffer[16];
                swprintf_s(buffer, L"%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
                saved = buffer;
            }
        }
        const auto mb = g_repository->GetStorageSizeBytes() / 1024.0 / 1024.0;
        wchar_t storage[256];
        swprintf_s(storage, L"本地数据：Data\\key-stats.db · 上次保存 %s · %.2f MB", saved.c_str(), mb);
        auto error = g_recorder ? g_recorder->LastError() : std::string();
        if (!error.empty()) {
            const auto wide = Utf8ToWide(" · 最近错误：" + error);
            wcscat_s(storage, wide.c_str());
        }
        SetWindowTextW(GetDlgItem(g_window, kIdStorage), storage);
        g_last_storage_refresh = now;
        if (g_range_preset != "Custom") {
            g_selected_range = ResolvePreset(g_range_preset);
            SetRangeEditors(*g_selected_range);
        }
        RefreshSelectedRange(force);
    } catch (const std::exception& exception) {
        SetWindowTextW(GetDlgItem(g_window, kIdHistory), L"总数量：不可用");
        SetWindowTextUtf8(GetDlgItem(g_window, kIdStorage), std::string("本地数据错误：") + exception.what());
        EnableWindow(GetDlgItem(g_window, kIdSave), FALSE);
        EnableWindow(GetDlgItem(g_window, kIdExport), FALSE);
    }
}

void InitializeStorage() {
    wchar_t module_path[MAX_PATH];
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    const auto exe = std::filesystem::path(module_path);
    const auto data = exe.parent_path() / "Data";
    const auto settings_u8 = (data / "settings.json").u8string();
    SettingsStore store(std::string(reinterpret_cast<const char*>(settings_u8.c_str()), settings_u8.size()));
    const auto settings = store.LoadOrCreate();
    g_repository = std::make_unique<KeyStatsRepository>(data / "key-stats.db");
    g_repository->Initialize();
    g_recorder = StatisticsRecorder::Create(*g_repository, std::chrono::seconds(settings.flush_interval_seconds));
    EnableWindow(GetDlgItem(g_window, kIdSave), TRUE);
    EnableWindow(GetDlgItem(g_window, kIdExport), TRUE);
    g_selected_range = ResolvePreset(g_range_preset);
    SetRangeEditors(*g_selected_range);
    RefreshStorageUi(true);
}

void SetPaused(bool paused) {
    g_counter.SetPaused(paused);
    if (g_tray) {
        g_tray->SetPaused(paused);
    }
    SetWindowTextW(GetDlgItem(g_window, kIdPause), paused ? L"恢复采集" : L"暂停采集");
    SetWindowTextW(GetDlgItem(g_window, kIdStatus), paused ? L"已暂停" : L"正在采集");
}

void ShowFromTray() {
    if (g_closing) {
        return;
    }
    ShowWindow(g_window, SW_SHOW);
    ShowWindow(g_window, SW_RESTORE);
    SetForegroundWindow(g_window);
}

void SaveNow() {
    if (!g_recorder) {
        return;
    }
    try {
        g_recorder->FlushNow();
        RefreshStorageUi(true);
    } catch (const std::exception& exception) {
        MessageBoxW(g_window, Utf8ToWide(exception.what()).c_str(), L"KeyStats", MB_ICONERROR);
    }
}

void ExitApplication(bool confirm) {
    if (g_closing) {
        return;
    }
    if (confirm) {
        if (MessageBoxW(IsWindowVisible(g_window) ? g_window : nullptr,
                        L"完整退出后将停止键盘和鼠标采集。\n\n确定退出 KeyStats 吗？", L"完整退出 KeyStats",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }
    g_closing = true;
    KillTimer(g_window, kRefreshTimer);
    g_raw.reset();
    try {
        g_recorder.reset();
    } catch (...) {
    }
    g_repository.reset();
    g_tray.reset();
    DestroyWindow(g_window);
}

void ApplyCustomRange() {
    SYSTEMTIME start_date{};
    SYSTEMTIME end_date{};
    DateTime_GetSystemtime(GetDlgItem(g_window, kIdStartDate), &start_date);
    DateTime_GetSystemtime(GetDlgItem(g_window, kIdEndDate), &end_date);
    wchar_t start_text[16]{};
    wchar_t end_text[16]{};
    GetWindowTextW(GetDlgItem(g_window, kIdStartTime), start_text, 16);
    GetWindowTextW(GetDlgItem(g_window, kIdEndTime), end_text, 16);
    int sh = 0, sm = 0, eh = 0, em = 0;
    if (swscanf_s(start_text, L"%d:%d", &sh, &sm) != 2 || swscanf_s(end_text, L"%d:%d", &eh, &em) != 2) {
        SetWindowTextW(GetDlgItem(g_window, kIdRangeError), L"时间格式应为 HH:mm，例如 09:30。");
        return;
    }
    std::tm start{};
    start.tm_year = start_date.wYear - 1900;
    start.tm_mon = start_date.wMonth - 1;
    start.tm_mday = start_date.wDay;
    start.tm_hour = sh;
    start.tm_min = sm;
    start.tm_isdst = -1;
    std::tm end{};
    end.tm_year = end_date.wYear - 1900;
    end.tm_mon = end_date.wMonth - 1;
    end.tm_mday = end_date.wDay;
    end.tm_hour = eh;
    end.tm_min = em;
    end.tm_isdst = -1;
    if (SendMessageW(GetDlgItem(g_window, kIdSnap), BM_GETCHECK, 0, 0) == BST_CHECKED) {
        start.tm_min -= start.tm_min % 10;
        start.tm_sec = 0;
        if (end.tm_min % 10 != 0 || end.tm_sec != 0) {
            end.tm_min += 10 - (end.tm_min % 10);
            if (end.tm_min >= 60) {
                end.tm_min -= 60;
                ++end.tm_hour;
            }
        }
        end.tm_sec = 0;
    }
    auto range = std::pair(ToMinuteStartUnix(LocalToUnix(start)), ToMinuteStartUnix(LocalToUnix(end)));
    if (range.second <= range.first) {
        SetWindowTextW(GetDlgItem(g_window, kIdRangeError), L"结束时间必须晚于开始时间。");
        return;
    }
    g_range_preset = "Custom";
    g_selected_range = range;
    SetWindowTextW(GetDlgItem(g_window, kIdRangeError), L"");
    RefreshSelectedRange(true);
}

bool RefreshLiveSelectedCounts() {
    if (!g_recorder || !g_selected_range || !g_live_bucket) {
        return false;
    }
    const auto live = g_recorder->GetCurrentSnapshot();
    if (live.bucket_start_utc != *g_live_bucket) {
        RefreshStorageUi(true);
        return false;
    }
    if (live.bucket_start_utc < g_selected_range->first || live.bucket_start_utc >= g_selected_range->second ||
        (g_last_live_counts && *g_last_live_counts == live.counts)) {
        return false;
    }
    g_last_live_counts = live.counts;
    auto display = g_selected_stored_counts;
    auto display_time = g_selected_stored_time;
    for (int i = 0; i < kKeyCount; ++i) {
        display[static_cast<std::size_t>(i)] += live.counts[static_cast<std::size_t>(i)];
    }
    AddLiveTime(display_time, live);
    ApplySelectedCounts(display, display_time, g_counter.GetSnapshot());
    return true;
}

void OnRefreshTick() {
    if (g_raw) {
        g_raw->ReconcileSystemShortcutState();
    }
    const auto snapshot = g_counter.GetSnapshot();
    SetWindowTextW(GetDlgItem(g_window, kIdTotal), FormatInt(static_cast<std::uint64_t>(snapshot.total_count)).c_str());
    if (snapshot.pressed_keys.empty()) {
        SetWindowTextW(GetDlgItem(g_window, kIdPressed), L"无");
    } else {
        std::vector<int> keys(snapshot.pressed_keys.begin(), snapshot.pressed_keys.end());
        std::sort(keys.begin(), keys.end());
        std::wstring text;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (i) text += L"  +  ";
            text += Utf8ToWide(KeyCatalog::Get(static_cast<KeyId>(keys[i])).display_name);
        }
        SetWindowTextW(GetDlgItem(g_window, kIdPressed), text.c_str());
    }
    if (g_raw) {
        SetWindowTextUtf8(GetDlgItem(g_window, kIdLastInput), g_raw->LastInputDescription());
        std::string unknown = "未识别输入：" + std::to_string(g_raw->UnrecognizedMakeCount());
        if (!g_raw->LastUnrecognizedDescription().empty()) {
            unknown += " · 最近 " + g_raw->LastUnrecognizedDescription();
        }
        SetWindowTextUtf8(GetDlgItem(g_window, kIdUnknown), unknown);
    }
    const bool counts_changed = RefreshLiveSelectedCounts();
    if (counts_changed) {
        SetHeatmapData(g_heatmap, g_selected_counts, snapshot.pressed_keys, CurrentScale());
    } else {
        SetHeatmapData(g_heatmap, g_selected_counts, snapshot.pressed_keys, CurrentScale());
    }
    if (SendMessageW(GetDlgItem(g_window, kIdActiveOnly), BM_GETCHECK, 0, 0) == BST_CHECKED) {
        RebuildList(snapshot);
    }
    RefreshStorageUi(false);
}

void Layout() {
    RECT client{};
    GetClientRect(g_window, &client);
    const int width = client.right;
    const int height = client.bottom;
    MoveWindow(g_tabs, 24, 390, width - 48, height - 450, TRUE);
    RECT tab{};
    GetClientRect(g_tabs, &tab);
    TabCtrl_AdjustRect(g_tabs, FALSE, &tab);
    MapWindowPoints(g_tabs, g_window, reinterpret_cast<POINT*>(&tab), 2);
    MoveWindow(g_heatmap, tab.left + 8, tab.top + 36, tab.right - tab.left - 16, tab.bottom - tab.top - 80, TRUE);
    MoveWindow(g_list, tab.left + 8, tab.top + 36, tab.right - tab.left - 16, tab.bottom - tab.top - 16, TRUE);
    const int selected = TabCtrl_GetCurSel(g_tabs);
    ShowWindow(g_heatmap, selected == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_window, kIdActiveOnly), selected == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_list, selected == 1 ? SW_SHOW : SW_HIDE);
    MoveWindow(g_timeline, 40, 156, width - 80, 40, TRUE);
}

HWND CreateChild(LPCWSTR class_name, LPCWSTR text, DWORD style, int x, int y, int w, int h, int id) {
    HWND child = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return child;
}

void CreateUi(HINSTANCE instance) {
    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                         L"Segoe UI");
    g_title_font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                               0, L"Segoe UI");
    CreateChild(L"STATIC", L"KeyStats", 0, 24, 12, 200, 28, 0);
    SendMessageW(GetWindow(g_window, GW_CHILD), WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
    CreateChild(L"STATIC", L"键盘与鼠标使用频率统计", 0, 24, 40, 280, 20, 0);
    CreateChild(L"BUTTON", L"登录 Windows 后自动启动", BS_AUTOCHECKBOX, 780, 18, 250, 24, kIdStartup);
    CreateChild(L"STATIC", L"正在采集", 0, 1050, 18, 120, 24, kIdStatus);
    CreateChild(L"STATIC", L"范围", 0, 24, 72, 40, 18, 0);
    HWND range = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 24, 90, 120, 200, kIdRange);
    for (auto* item : {L"今天", L"最近 10 分钟", L"最近 7 天", L"最近 30 天", L"全部", L"自定义"}) {
        SendMessageW(range, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    SendMessageW(range, CB_SETCURSEL, 0, 0);
    CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT, 156, 90, 130, 26, g_window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStartDate)), instance, nullptr);
    HWND start_time = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWN, 294, 90, 80, 220, kIdStartTime);
    CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT, 384, 90, 130, 26, g_window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEndDate)), instance, nullptr);
    HWND end_time = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWN, 522, 90, 80, 220, kIdEndTime);
    for (int minute = 0; minute < 24 * 60; minute += 10) {
        wchar_t text[8];
        swprintf_s(text, L"%02d:%02d", minute / 60, minute % 60);
        SendMessageW(start_time, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(end_time, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    CreateChild(L"BUTTON", L"10 分钟吸附", BS_AUTOCHECKBOX, 614, 92, 120, 22, kIdSnap);
    SendMessageW(GetDlgItem(g_window, kIdSnap), BM_SETCHECK, BST_CHECKED, 0);
    CreateChild(L"BUTTON", L"应用范围", BS_PUSHBUTTON, 740, 88, 90, 28, kIdApply);
    CreateChild(L"STATIC", L"", 0, 840, 92, 220, 22, kIdRangeError);
    HWND scale = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 1080, 90, 110, 80, kIdScale);
    SendMessageW(scale, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"平方根"));
    SendMessageW(scale, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"线性"));
    SendMessageW(scale, CB_SETCURSEL, 0, 0);
    CreateChild(L"STATIC", L"时段热力图 · 所选范围按每天相同的 10 分钟时段汇总", 0, 24, 130, 520, 18, 0);
    CreateChild(L"STATIC", L"暂无输入", 0, 900, 130, 280, 18, kIdTimelineSummary);
    g_timeline = CreateTimelineControl(g_window, kIdTimeline, RECT{24, 156, 1200, 196});
    CreateChild(L"STATIC", L"本次运行", 0, 40, 220, 120, 18, 0);
    CreateChild(L"STATIC", L"0", 0, 40, 240, 200, 40, kIdTotal);
    CreateChild(L"STATIC", L"总数量：正在加载…", 0, 40, 280, 280, 20, kIdHistory);
    CreateChild(L"BUTTON", L"暂停采集", BS_PUSHBUTTON, 40, 310, 100, 30, kIdPause);
    CreateChild(L"BUTTON", L"清零本次显示", BS_PUSHBUTTON, 148, 310, 120, 30, kIdClear);
    CreateChild(L"BUTTON", L"立即保存", BS_PUSHBUTTON, 276, 310, 90, 30, kIdSave);
    CreateChild(L"BUTTON", L"导出 CSV", BS_PUSHBUTTON, 374, 310, 90, 30, kIdExport);
    EnableWindow(GetDlgItem(g_window, kIdSave), FALSE);
    EnableWindow(GetDlgItem(g_window, kIdExport), FALSE);
    CreateChild(L"STATIC", L"当前按下中的键或按钮", 0, 520, 220, 220, 18, 0);
    CreateChild(L"STATIC", L"无", 0, 520, 240, 680, 28, kIdPressed);
    CreateChild(L"STATIC", L"等待输入…", 0, 520, 274, 680, 20, kIdLastInput);
    CreateChild(L"STATIC", L"未识别输入：0", 0, 520, 296, 680, 20, kIdUnknown);
    CreateChild(L"STATIC", L"本地数据：正在初始化…", 0, 520, 318, 680, 20, kIdStorage);
    g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 24, 310, 1200, 480,
                             g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabs)), instance, nullptr);
    SendMessageW(g_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    wchar_t tab1[] = L"键盘热力图";
    tab.pszText = tab1;
    TabCtrl_InsertItem(g_tabs, 0, &tab);
    wchar_t tab2[] = L"详细列表";
    tab.pszText = tab2;
    TabCtrl_InsertItem(g_tabs, 1, &tab);
    g_heatmap = CreateHeatmapControl(g_window, kIdHeatmap, RECT{40, 360, 1220, 720});
    CreateChild(L"BUTTON", L"只显示有次数或按下中的键", BS_AUTOCHECKBOX, 40, 360, 260, 22, kIdActiveOnly);
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 40, 386, 1180, 360, g_window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)), instance, nullptr);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.cx = 220;
    wchar_t c0[] = L"按键/按钮";
    column.pszText = c0;
    ListView_InsertColumn(g_list, 0, &column);
    column.cx = 120;
    wchar_t c1[] = L"区域";
    column.pszText = c1;
    ListView_InsertColumn(g_list, 1, &column);
    column.cx = 100;
    wchar_t c2[] = L"次数";
    column.pszText = c2;
    ListView_InsertColumn(g_list, 2, &column);
    column.cx = 80;
    wchar_t c3[] = L"状态";
    column.pszText = c3;
    ListView_InsertColumn(g_list, 3, &column);
    column.cx = 160;
    wchar_t c4[] = L"按键标识";
    column.pszText = c4;
    ListView_InsertColumn(g_list, 4, &column);
    CreateChild(L"STATIC", L"正在加载范围…", 0, 40, 730, 400, 18, kIdHeatRange);
    CreateChild(L"STATIC", L"所选范围总计：0", 0, 500, 730, 220, 18, kIdHeatTotal);
    CreateChild(L"STATIC", L"尚未更新", 0, 980, 730, 200, 18, kIdHeatUpdated);
    CreateChild(L"STATIC", L"所有数据仅保存在本机 · 不记录输入内容、输入顺序、使用中的应用或鼠标位置", 0, 24, 760,
                900, 20, 0);
    EnableCustomEditors(false);
}

void OnCommand(WPARAM w_param) {
    const int id = LOWORD(w_param);
    const int code = HIWORD(w_param);
    if (g_tray && g_tray->HandleCommand(id)) {
        return;
    }
    if (id == kIdPause) {
        SetPaused(!g_counter.IsPaused());
    } else if (id == kIdClear) {
        if (MessageBoxW(g_window, L"只清空本次运行计数。已经保存的历史数据不会删除。\n\n确定继续吗？",
                        L"确认清零本次显示", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            g_counter.ClearCounts(true);
            if (g_raw) g_raw->ClearDiagnostics();
        }
    } else if (id == kIdSave) {
        SaveNow();
    } else if (id == kIdExport) {
        if (!g_repository || !g_selected_range) return;
        wchar_t path[MAX_PATH] = L"key-stats.csv";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_window;
        ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"csv";
        if (GetSaveFileNameW(&ofn)) {
            try {
                if (g_recorder) g_recorder->FlushNow();
                g_repository->ExportCsv(g_selected_range->first, g_selected_range->second, path);
                MessageBoxW(g_window, L"CSV 导出完成。", L"KeyStats", MB_OK | MB_ICONINFORMATION);
            } catch (const std::exception& exception) {
                MessageBoxW(g_window, Utf8ToWide(std::string("导出失败：") + exception.what()).c_str(), L"KeyStats",
                            MB_ICONERROR);
            }
        }
    } else if (id == kIdApply) {
        ApplyCustomRange();
    } else if (id == kIdStartup && code == BN_CLICKED && !g_startup_changing && g_startup) {
        const bool requested = SendMessageW(GetDlgItem(g_window, kIdStartup), BM_GETCHECK, 0, 0) == BST_CHECKED;
        try {
            g_startup->SetEnabled(requested);
        } catch (const std::exception& exception) {
            g_startup_changing = true;
            SendMessageW(GetDlgItem(g_window, kIdStartup), BM_SETCHECK, requested ? BST_UNCHECKED : BST_CHECKED, 0);
            g_startup_changing = false;
            MessageBoxW(g_window, Utf8ToWide(std::string("无法修改 Windows 启动项。\n\n") + exception.what()).c_str(),
                        L"KeyStats", MB_ICONERROR);
        }
    } else if (id == kIdRange && code == CBN_SELCHANGE) {
        const auto index = SendMessageW(GetDlgItem(g_window, kIdRange), CB_GETCURSEL, 0, 0);
        static const char* presets[] = {"Today", "Recent10", "Recent7Days", "Recent30Days", "All", "Custom"};
        g_range_preset = presets[index];
        EnableCustomEditors(g_range_preset == "Custom");
        if (g_range_preset != "Custom") {
            g_selected_range = ResolvePreset(g_range_preset);
            SetRangeEditors(*g_selected_range);
            RefreshSelectedRange(true);
        }
    } else if (id == kIdScale && code == CBN_SELCHANGE) {
        SetHeatmapData(g_heatmap, g_selected_counts, g_counter.GetSnapshot().pressed_keys, CurrentScale());
    } else if (id == kIdActiveOnly) {
        RebuildList(g_counter.GetSnapshot());
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    if (g_raw && g_raw->HandleMessage(message, w_param, l_param)) {
        return 0;
    }
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
            info->ptMinTrackSize.x = 1180;
            info->ptMinTrackSize.y = 820;
            return 0;
        }
        case WM_CREATE:
            g_window = hwnd;
            CreateUi(reinterpret_cast<LPCREATESTRUCT>(l_param)->hInstance);
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_COMMAND:
            OnCommand(w_param);
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(l_param)->hwndFrom == g_tabs &&
                reinterpret_cast<NMHDR*>(l_param)->code == TCN_SELCHANGE) {
                Layout();
            }
            return 0;
        case WM_TIMER:
            if (w_param == kRefreshTimer) {
                OnRefreshTick();
            }
            return 0;
        case WM_APP + 18:
            ShowFromTray();
            return 0;
        case WM_APP + 19:
            ExitApplication(false);
            return 0;
        case TrayIcon::kCallbackMessage:
            if (l_param == WM_LBUTTONDBLCLK || l_param == WM_LBUTTONUP) {
                ShowFromTray();
            } else if (l_param == WM_RBUTTONUP && g_tray) {
                g_tray->ShowContextMenu();
            }
            return 0;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

}  // namespace

int RunApplication(HINSTANCE instance, bool start_in_background, int show_command) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_LISTVIEW_CLASSES |
                                                        ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RegisterCustomControls(instance);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(245, 246, 248));
    wc.lpszClassName = L"KeyStatsMainWindow";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_KEYSTATS));
    RegisterClassW(&wc);

    HWND window = CreateWindowExW(0, wc.lpszClassName, L"KeyStats · 键盘与鼠标使用统计",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1360, 900,
                                  nullptr, nullptr, instance, nullptr);
    const auto icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_KEYSTATS));
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    g_window = window;
    wchar_t module_path[MAX_PATH];
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    g_startup = std::make_unique<StartupRegistration>(module_path);
    g_startup_changing = true;
    SendMessageW(GetDlgItem(window, kIdStartup), BM_SETCHECK,
                 g_startup->IsEnabledForCurrentExecutable() ? BST_CHECKED : BST_UNCHECKED, 0);
    g_startup_changing = false;
    g_tray = std::make_unique<TrayIcon>(window, ShowFromTray, [] { SetPaused(!g_counter.IsPaused()); }, SaveNow,
                                        [] { ExitApplication(true); });
    try {
        g_raw = std::make_unique<RawInputSource>(window, g_counter, [](KeyId id) {
            if (g_recorder) g_recorder->RecordKeyPress(id);
        });
        InitializeStorage();
    } catch (const std::exception& exception) {
        SetWindowTextW(GetDlgItem(window, kIdStatus), L"启动失败");
        MessageBoxW(window, Utf8ToWide(std::string("无法启动键盘采集。\n\n") + exception.what()).c_str(), L"KeyStats",
                    MB_ICONERROR);
    }
    SetTimer(window, kRefreshTimer, 150, nullptr);
    Layout();
    if (!start_in_background) {
        ShowWindow(window, show_command);
        UpdateWindow(window);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace keystats
