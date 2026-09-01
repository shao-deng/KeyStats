#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

namespace keystats {

class TrayIcon {
public:
    TrayIcon(HWND owner, std::function<void()> open, std::function<void()> toggle_pause,
             std::function<void()> save_now, std::function<void()> exit);
    ~TrayIcon();

    void SetPaused(bool paused);
    void ShowHiddenNotification();
    void ShowContextMenu();
    bool HandleCommand(int command_id);

    static constexpr UINT kCallbackMessage = WM_APP + 17;
    static constexpr UINT kCmdOpen = 4001;
    static constexpr UINT kCmdPause = 4002;
    static constexpr UINT kCmdSave = 4003;
    static constexpr UINT kCmdExit = 4004;

private:
    HWND owner_;
    NOTIFYICONDATAW data_{};
    std::function<void()> open_;
    std::function<void()> toggle_pause_;
    std::function<void()> save_now_;
    std::function<void()> exit_;
    bool hidden_notification_shown_ = false;
    bool paused_ = false;
};

}  // namespace keystats
