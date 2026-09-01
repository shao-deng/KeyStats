#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace keystats {

struct AppSettings {
    int version = 1;
    int flush_interval_seconds = 30;
    bool count_key_repeat = false;
    std::optional<int> retention_days;

    void Validate() const {
        if (version != 1) {
            throw std::runtime_error("不支持的设置版本：" + std::to_string(version) + "。");
        }
        if (flush_interval_seconds < 5 || flush_interval_seconds > 300) {
            throw std::runtime_error("FlushIntervalSeconds 必须在 5～300 秒之间。");
        }
        if (retention_days.has_value() && *retention_days <= 0) {
            throw std::runtime_error("RetentionDays 必须为空或大于 0。");
        }
    }
};

class SettingsStore {
public:
    explicit SettingsStore(std::string settings_path);

    AppSettings LoadOrCreate();
    void Save(const AppSettings& settings);

    const std::string& Path() const { return settings_path_; }

private:
    std::string settings_path_;
};

}  // namespace keystats
