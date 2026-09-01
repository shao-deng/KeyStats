#pragma once

#include <string>

namespace keystats {

class StartupRegistration {
public:
    explicit StartupRegistration(std::wstring executable_path);

    bool IsEnabledForCurrentExecutable() const;
    void SetEnabled(bool enabled);

private:
    std::wstring command_;
};

}  // namespace keystats
