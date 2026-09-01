#include "tray.hpp"

#include "utf.hpp"

namespace keystats {

TrayIcon::TrayIcon(HWND owner, std::function<void()> open, std::function<void()> toggle_pause,
                   std::function<void()> save_now, std::function<void()> exit)
    : owner_(owner),
      open_(std::move(open)),
      toggle_pause_(std::move(toggle_pause)),
      save_now_(std::move(save_now)),
      exit_(std::move(exit)) {
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data_.szTip, L"KeyStats · 正在采集");
    Shell_NotifyIconW(NIM_ADD, &data_);
}

TrayIcon::~TrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &data_);
}

void TrayIcon::SetPaused(bool paused) {
    paused_ = paused;
    wcscpy_s(data_.szTip, paused ? L"KeyStats · 已暂停" : L"KeyStats · 正在采集");
    data_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::ShowHiddenNotification() {
    if (hidden_notification_shown_) {
        return;
    }
    hidden_notification_shown_ = true;
    data_.uFlags = NIF_INFO;
    wcscpy_s(data_.szInfoTitle, L"KeyStats 仍在后台采集");
    wcscpy_s(data_.szInfo, L"双击托盘图标可重新打开；选择“完整退出”才会停止采集。");
    data_.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::ShowContextMenu() {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdOpen, L"打开统计窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdPause, paused_ ? L"恢复采集" : L"暂停采集");
    AppendMenuW(menu, MF_STRING, kCmdSave, L"立即保存");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"完整退出");
    SetForegroundWindow(owner_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, owner_, nullptr);
    DestroyMenu(menu);
}

bool TrayIcon::HandleCommand(int command_id) {
    switch (command_id) {
        case kCmdOpen:
            if (open_) open_();
            return true;
        case kCmdPause:
            if (toggle_pause_) toggle_pause_();
            return true;
        case kCmdSave:
            if (save_now_) save_now_();
            return true;
        case kCmdExit:
            if (exit_) exit_();
            return true;
        default:
            return false;
    }
}

}  // namespace keystats
