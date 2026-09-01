#include "settings_store.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace keystats {
namespace {

std::optional<int> ParseIntField(const std::string& json, const std::string& name) {
    const auto key = "\"" + name + "\"";
    const auto pos = json.find(key);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    auto cursor = json.find(':', pos + key.size());
    if (cursor == std::string::npos) {
        return std::nullopt;
    }
    ++cursor;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (cursor < json.size() && json.compare(cursor, 4, "null") == 0) {
        return std::nullopt;
    }
    std::size_t consumed = 0;
    const auto value = std::stoi(json.substr(cursor), &consumed);
    return value;
}

bool ParseBoolField(const std::string& json, const std::string& name, bool fallback) {
    const auto key = "\"" + name + "\"";
    const auto pos = json.find(key);
    if (pos == std::string::npos) {
        return fallback;
    }
    auto cursor = json.find(':', pos + key.size());
    if (cursor == std::string::npos) {
        return fallback;
    }
    ++cursor;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (json.compare(cursor, 4, "true") == 0) {
        return true;
    }
    if (json.compare(cursor, 5, "false") == 0) {
        return false;
    }
    return fallback;
}

std::string Serialize(const AppSettings& settings) {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"version\": " << settings.version << ",\n";
    stream << "  \"flushIntervalSeconds\": " << settings.flush_interval_seconds << ",\n";
    stream << "  \"countKeyRepeat\": " << (settings.count_key_repeat ? "true" : "false") << ",\n";
    stream << "  \"retentionDays\": ";
    if (settings.retention_days) {
        stream << *settings.retention_days;
    } else {
        stream << "null";
    }
    stream << "\n}\n";
    return stream.str();
}

}  // namespace

SettingsStore::SettingsStore(std::string settings_path) : settings_path_(std::move(settings_path)) {}

AppSettings SettingsStore::LoadOrCreate() {
    if (!std::filesystem::exists(settings_path_)) {
        AppSettings defaults;
        Save(defaults);
        return defaults;
    }
    std::ifstream stream(settings_path_, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("无法读取设置文件。");
    }
    const std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    AppSettings settings;
    if (const auto version = ParseIntField(json, "version")) {
        settings.version = *version;
    }
    if (const auto flush = ParseIntField(json, "flushIntervalSeconds")) {
        settings.flush_interval_seconds = *flush;
    }
    settings.count_key_repeat = ParseBoolField(json, "countKeyRepeat", false);
    settings.retention_days = ParseIntField(json, "retentionDays");
    settings.Validate();
    return settings;
}

void SettingsStore::Save(const AppSettings& settings) {
    settings.Validate();
    const auto directory = std::filesystem::path(settings_path_).parent_path();
    std::filesystem::create_directories(directory);
    auto temporary = settings_path_ + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("无法写入设置文件。");
        }
        stream << Serialize(settings);
    }
    std::error_code error;
    std::filesystem::remove(settings_path_, error);
    std::filesystem::rename(temporary, settings_path_);
}

}  // namespace keystats
