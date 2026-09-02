#pragma once

#include "storage_types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace keystats {

class KeyStatsRepository {
public:
    static constexpr int kCurrentSchemaVersion = 1;

    explicit KeyStatsRepository(std::filesystem::path database_path);
    ~KeyStatsRepository();

    KeyStatsRepository(const KeyStatsRepository&) = delete;
    KeyStatsRepository& operator=(const KeyStatsRepository&) = delete;

    void Initialize();
    std::optional<MinuteBucketSnapshot> GetMinuteBucket(long long bucket_start_utc);
    void UpsertMinuteBucket(const MinuteBucketSnapshot& snapshot);
    StatisticsAggregate QueryAggregate(long long start_utc, long long end_utc);
    std::optional<StoredDataRange> GetDataRange();
    std::uint64_t QueryTotalCount(std::optional<long long> excluded_bucket_start_utc = std::nullopt);
    std::vector<TenMinuteBucketTotal> QueryTenMinuteTotals(long long start_utc, long long end_utc);
    void ExportCsv(long long start_utc, long long end_utc, const std::filesystem::path& destination);
    std::string CheckIntegrity();
    long long GetStorageSizeBytes() const;

    const std::filesystem::path& DatabasePath() const { return database_path_; }

    // 测试用：直接执行 SQL。
    void Execute(const std::string& sql);
    void BindInsertLegacyMinute(long long start, int keymap_version, const std::vector<std::uint8_t>& blob, long long total);

private:
    sqlite3* Open() const;
    void RebuildTenMinuteRollup(sqlite3* db, long long changed_minute_start);
    void AccumulateRows(sqlite3* db, const char* table, long long start_utc, long long end_utc,
                        std::array<std::uint64_t, kKeyCount>& destination);
    static void ValidateKeyMapVersion(int version);
    static void ValidateRange(long long start_utc, long long end_utc);

    std::filesystem::path database_path_;
};

}  // namespace keystats
