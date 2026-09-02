#include "key_catalog.hpp"
#include "key_id.hpp"

#include <array>
#include <stdexcept>
#include <string_view>

namespace keystats {
namespace {

constexpr std::string_view CategoryOf(KeyId id) {
    const auto value = ToIndex(id);
    if ((value >= ToIndex(KeyId::F1) && value <= ToIndex(KeyId::Pause)) ||
        (value >= ToIndex(KeyId::F13) && value <= ToIndex(KeyId::F24))) {
        return "功能键";
    }
    if (value >= ToIndex(KeyId::Insert) && value <= ToIndex(KeyId::ArrowRight)) {
        return "导航键";
    }
    if (value >= ToIndex(KeyId::NumLock) && value <= ToIndex(KeyId::NumpadDecimal)) {
        return "数字键盘";
    }
    if ((value >= ToIndex(KeyId::BrowserBack) && value <= ToIndex(KeyId::LaunchApplication2)) ||
        id == KeyId::Sleep) {
        return "扩展键";
    }
    if (id == KeyId::MouseLeftButton || id == KeyId::MouseRightButton) {
        return "鼠标";
    }
    return "主键区";
}

constexpr std::array<std::string_view, kKeyCount> kDisplayNames = {{
    "Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Print Screen", "Scroll Lock", "Pause",
    "` / ~", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "- / _", "= / +", "Backspace",
    "Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[ / {", "] / }", "\\ / |",
    "Caps Lock", "A", "S", "D", "F", "G", "H", "J", "K", "L", "; / :", "' / \"", "Enter",
    "左 Shift", "Z", "X", "C", "V", "B", "N", "M", ", / <", ". / >", "/ / ?", "右 Shift",
    "左 Ctrl", "左 Win", "左 Alt", "Space", "右 Alt", "右 Win", "菜单", "右 Ctrl",
    "Insert", "Home", "Page Up", "Delete", "End", "Page Down",
    "↑", "←", "↓", "→",
    "Num Lock", "数字键盘 /", "数字键盘 *", "数字键盘 -",
    "数字键盘 7", "数字键盘 8", "数字键盘 9", "数字键盘 +",
    "数字键盘 4", "数字键盘 5", "数字键盘 6",
    "数字键盘 1", "数字键盘 2", "数字键盘 3", "数字键盘 Enter",
    "数字键盘 0", "数字键盘 .",
    "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
    "浏览器后退", "浏览器前进", "浏览器刷新", "浏览器停止", "浏览器搜索", "浏览器收藏", "浏览器主页",
    "静音", "音量减", "音量加",
    "下一曲", "上一曲", "停止播放", "播放/暂停",
    "邮件", "媒体", "应用 1", "应用 2",
    "Sleep", "OEM 102",
    "鼠标左键", "鼠标右键"
}};

// The last entry must be 鼠标右键. I'll fix in a moment if I wrote 鼠标右按钮.

}  // namespace

std::string_view KeyIdName(KeyId id) {
    return kDisplayNames.at(static_cast<std::size_t>(ToIndex(id)));
}

namespace {

const std::array<KeyDefinition, kKeyCount>& BuildDefinitions() {
    static const auto definitions = [] {
        std::array<KeyDefinition, kKeyCount> items{};
        for (int index = 0; index < kKeyCount; ++index) {
            const auto id = static_cast<KeyId>(index);
            items[static_cast<std::size_t>(index)] = KeyDefinition{
                id, kDisplayNames[static_cast<std::size_t>(index)], CategoryOf(id), index};
        }
        return items;
    }();
    return definitions;
}

}  // namespace

std::span<const KeyDefinition> KeyCatalog::Definitions() {
    return BuildDefinitions();
}

const KeyDefinition& KeyCatalog::Get(KeyId id) {
    const auto index = ToIndex(id);
    if (index < 0 || index >= Count) {
        throw std::out_of_range("KeyId 超出目录范围。");
    }
    return BuildDefinitions()[static_cast<std::size_t>(index)];
}

}  // namespace keystats
