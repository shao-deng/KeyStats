#pragma once

#include "../core/key_id.hpp"
#include "../core/keyboard_snapshot.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <unordered_set>

namespace keystats {

enum class HeatScaleMode { SquareRoot, Linear };

void RegisterCustomControls(HINSTANCE instance);
HWND CreateHeatmapControl(HWND parent, int control_id, RECT bounds);
HWND CreateTimelineControl(HWND parent, int control_id, RECT bounds);
void SetHeatmapData(HWND heatmap, const std::array<std::uint64_t, kKeyCount>& counts,
                    const std::unordered_set<int>& pressed, HeatScaleMode mode);
void SetTimelineData(HWND timeline, const std::array<std::uint64_t, 144>& counts);
std::wstring TimelineHitTooltip(HWND timeline, int x);

}  // namespace keystats
