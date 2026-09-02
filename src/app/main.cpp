#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "application.hpp"

#include <string>
#include <vector>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\KeyStats.SingleInstance.v1";
constexpr wchar_t kShowName[] = L"Local\\KeyStats.Show.v1";
constexpr wchar_t kExitName[] = L"Local\\KeyStats.Exit.v1";

bool HasArg(const std::vector<std::wstring>& args, const wchar_t* value) {
    for (const auto& arg : args) {
        if (_wcsicmp(arg.c_str(), value) == 0) {
            return true;
        }
    }
    return false;
}

struct SignalState {
    HANDLE show = nullptr;
    HANDLE exit_event = nullptr;
};

DWORD WINAPI SignalThread(LPVOID param) {
    auto* state = static_cast<SignalState*>(param);
    HANDLE wait[2] = {state->show, state->exit_event};
    while (true) {
        const auto result = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
        HWND hwnd = nullptr;
        for (int i = 0; i < 100 && hwnd == nullptr; ++i) {
            hwnd = FindWindowW(L"KeyStatsMainWindow", nullptr);
            if (!hwnd) {
                Sleep(50);
            }
        }
        if (!hwnd) {
            continue;
        }
        if (result == WAIT_OBJECT_0) {
            PostMessageW(hwnd, WM_APP + 18, 0, 0);
        } else if (result == WAIT_OBJECT_0 + 1) {
            PostMessageW(hwnd, WM_APP + 19, 0, 0);
            break;
        } else {
            break;
        }
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }

    const bool background = HasArg(args, L"--background");
    const bool request_exit = HasArg(args, L"--exit");

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    const bool first = GetLastError() != ERROR_ALREADY_EXISTS;
    if (!first) {
        if (!(background && !request_exit)) {
            HANDLE signal = OpenEventW(EVENT_MODIFY_STATE, FALSE, request_exit ? kExitName : kShowName);
            if (signal) {
                SetEvent(signal);
                CloseHandle(signal);
            }
        }
        if (mutex) {
            CloseHandle(mutex);
        }
        return 0;
    }
    if (request_exit) {
        if (mutex) {
            CloseHandle(mutex);
        }
        return 0;
    }

    SignalState state;
    state.show = CreateEventW(nullptr, FALSE, FALSE, kShowName);
    state.exit_event = CreateEventW(nullptr, FALSE, FALSE, kExitName);
    HANDLE thread = CreateThread(nullptr, 0, SignalThread, &state, 0, nullptr);

    const int result = keystats::RunApplication(instance, background, show_command);

    if (state.exit_event) {
        SetEvent(state.exit_event);
    }
    if (thread) {
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
    }
    if (state.show) CloseHandle(state.show);
    if (state.exit_event) CloseHandle(state.exit_event);
    if (mutex) CloseHandle(mutex);
    return result;
}
