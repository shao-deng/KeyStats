#include "startup.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdexcept>

namespace keystats {

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"KeyStats";
}

StartupRegistration::StartupRegistration(std::wstring executable_path) {
    command_ = L"\"" + executable_path + L"\" --background";
}

bool StartupRegistration::IsEnabledForCurrentExecutable() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[1024];
    DWORD size = sizeof(value);
    DWORD type = 0;
    const auto status = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }
    return _wcsicmp(value, command_.c_str()) == 0;
}

void StartupRegistration::SetEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        throw std::runtime_error("无法打开当前用户的 Windows 启动项。");
    }
    if (enabled) {
        const auto bytes = static_cast<DWORD>((command_.size() + 1) * sizeof(wchar_t));
        const auto status = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command_.c_str()), bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS) {
            throw std::runtime_error("无法写入 Windows 启动项。");
        }
    } else {
        RegDeleteValueW(key, kValueName);
        RegCloseKey(key);
    }
}

}  // namespace keystats
