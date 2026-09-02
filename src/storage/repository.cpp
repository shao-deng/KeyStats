#include "repository.hpp"

#include "count_vector_codec.hpp"
#include "time_buckets.hpp"
#include "utc_clock.hpp"

#include "../core/key_catalog.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace keystats {
namespace {

void CheckSqlite(int rc, sqlite3* db, const char* what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const auto* message = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        throw std::runtime_error(std::string(what) + "：" + (message ? message : "未知错误"));
    }
}

void Exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

std::string ScalarText(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> guard(stmt, sqlite3_finalize);
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return {};
    }
    CheckSqlite(rc, db, "step");
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    return text ? std::string(text) : std::string();
}

long long ScalarLong(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> guard(stmt, sqlite3_finalize);
    const auto rc = sqlite3_step(stmt);
    CheckSqlite(rc, db, "step");
    return sqlite3_column_int64(stmt, 0);
}

void AddCounts(std::array<std::uint64_t, kKeyCount>& destination, const std::array<std::uint32_t, kKeyCount>& source) {
    for (int index = 0; index < kKeyCount; ++index) {
        destination[static_cast<std::size_t>(index)] += source[static_cast<std::size_t>(index)];
    }
}

std::string CsvEscape(std::string_view value) {
    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }
    escaped += '"';
    return escaped;
}

std::string FormatLocalMinute(long long unix_seconds) {
    const std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

const char* kCreateSchema = R"(
CREATE TABLE IF NOT EXISTS minute_buckets (
    bucket_start_utc INTEGER PRIMARY KEY CHECK(bucket_start_utc % 60 = 0),
    keymap_version INTEGER NOT NULL,
    counts BLOB NOT NULL,
    total_count INTEGER NOT NULL CHECK(total_count >= 0),
    updated_at_utc INTEGER NOT NULL
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS rollup_10m (
    bucket_start_utc INTEGER PRIMARY KEY CHECK(bucket_start_utc % 600 = 0),
    keymap_version INTEGER NOT NULL,
    counts BLOB NOT NULL,
    total_count INTEGER NOT NULL CHECK(total_count >= 0)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS app_meta (
    name TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;

INSERT INTO app_meta(name, value) VALUES('schema_version', '1')
ON CONFLICT(name) DO UPDATE SET value = excluded.value;
PRAGMA user_version=1;
)";

}  // namespace

KeyStatsRepository::KeyStatsRepository(std::filesystem::path database_path)
    : database_path_(std::filesystem::absolute(std::move(database_path))) {}

KeyStatsRepository::~KeyStatsRepository() = default;

sqlite3* KeyStatsRepository::Open() const {
    sqlite3* db = nullptr;
    const auto path = database_path_.u8string();
    CheckSqlite(sqlite3_open_v2(reinterpret_cast<const char*>(path.c_str()), &db,
                                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr),
                db, "打开数据库");
    try {
        Exec(db, "PRAGMA busy_timeout=5000;");
        Exec(db, "PRAGMA foreign_keys=ON;");
        Exec(db, "PRAGMA synchronous=NORMAL;");
        Exec(db, "PRAGMA wal_autocheckpoint=256;");
    } catch (...) {
        sqlite3_close(db);
        throw;
    }
    return db;
}

void KeyStatsRepository::Initialize() {
    std::filesystem::create_directories(database_path_.parent_path());
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    Exec(db, "PRAGMA journal_mode=WAL;");
    const auto integrity = ScalarText(db, "PRAGMA quick_check;");
    if (integrity != "ok") {
        throw std::runtime_error("SQLite 快速完整性检查失败：" + integrity);
    }
    const auto schema_version = ScalarLong(db, "PRAGMA user_version;");
    if (schema_version > kCurrentSchemaVersion) {
        throw std::runtime_error("数据库版本高于当前程序支持的版本，已拒绝修改。");
    }
    if (schema_version == 0) {
        Exec(db, kCreateSchema);
    }
}

void KeyStatsRepository::ValidateKeyMapVersion(int version) {
    if (version < CountVectorCodec::kMinimumSupportedKeyMapVersion ||
        version > CountVectorCodec::kKeyMapVersion) {
        throw std::runtime_error("不支持的 Key Map 版本：" + std::to_string(version) + "。");
    }
}

void KeyStatsRepository::ValidateRange(long long start_utc, long long end_utc) {
    if (start_utc % kMinuteSeconds != 0 || end_utc % kMinuteSeconds != 0) {
        throw std::invalid_argument("查询范围必须对齐到整分钟。");
    }
    if (end_utc <= start_utc) {
        throw std::invalid_argument("结束时间必须晚于开始时间。");
    }
}

std::optional<MinuteBucketSnapshot> KeyStatsRepository::GetMinuteBucket(long long bucket_start_utc) {
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db,
                                   "SELECT keymap_version, counts FROM minute_buckets WHERE bucket_start_utc = ?1;", -1,
                                   &stmt, nullptr),
                db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
    sqlite3_bind_int64(stmt, 1, bucket_start_utc);
    const auto rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    CheckSqlite(rc, db, "step");
    ValidateKeyMapVersion(sqlite3_column_int(stmt, 0));
    const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 1));
    const auto bytes = sqlite3_column_bytes(stmt, 1);
    MinuteBucketSnapshot snapshot;
    snapshot.bucket_start_utc = bucket_start_utc;
    snapshot.counts = CountVectorCodec::Decode({blob, static_cast<std::size_t>(bytes)});
    return snapshot;
}

void KeyStatsRepository::RebuildTenMinuteRollup(sqlite3* db, long long changed_minute_start) {
    const auto rollup_start = ToTenMinuteStart(changed_minute_start);
    std::array<std::uint64_t, kKeyCount> rollup_counts{};
    sqlite3_stmt* query = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db,
                                   "SELECT keymap_version, counts FROM minute_buckets "
                                   "WHERE bucket_start_utc >= ?1 AND bucket_start_utc < ?2;",
                                   -1, &query, nullptr),
                db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query_guard(query, sqlite3_finalize);
    sqlite3_bind_int64(query, 1, rollup_start);
    sqlite3_bind_int64(query, 2, rollup_start + kTenMinuteSeconds);
    while (true) {
        const auto rc = sqlite3_step(query);
        if (rc == SQLITE_DONE) {
            break;
        }
        CheckSqlite(rc, db, "step");
        ValidateKeyMapVersion(sqlite3_column_int(query, 0));
        const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(query, 1));
        const auto bytes = sqlite3_column_bytes(query, 1);
        AddCounts(rollup_counts, CountVectorCodec::Decode({blob, static_cast<std::size_t>(bytes)}));
    }

    std::array<std::uint32_t, kKeyCount> encoded{};
    long long total = 0;
    for (int index = 0; index < kKeyCount; ++index) {
        encoded[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(rollup_counts[static_cast<std::size_t>(index)]);
        total += encoded[static_cast<std::size_t>(index)];
    }
    const auto blob = CountVectorCodec::Encode(encoded);
    sqlite3_stmt* upsert = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db,
                                   "INSERT INTO rollup_10m (bucket_start_utc, keymap_version, counts, total_count) "
                                   "VALUES (?1, ?2, ?3, ?4) "
                                   "ON CONFLICT(bucket_start_utc) DO UPDATE SET "
                                   "keymap_version = excluded.keymap_version, counts = excluded.counts, "
                                   "total_count = excluded.total_count;",
                                   -1, &upsert, nullptr),
                db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> upsert_guard(upsert, sqlite3_finalize);
    sqlite3_bind_int64(upsert, 1, rollup_start);
    sqlite3_bind_int(upsert, 2, CountVectorCodec::kKeyMapVersion);
    sqlite3_bind_blob(upsert, 3, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(upsert, 4, total);
    CheckSqlite(sqlite3_step(upsert), db, "step");
}

void KeyStatsRepository::UpsertMinuteBucket(const MinuteBucketSnapshot& snapshot) {
    if (snapshot.bucket_start_utc % kMinuteSeconds != 0) {
        throw std::invalid_argument("分钟分段起点没有对齐到整分钟。");
    }
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    Exec(db, "BEGIN IMMEDIATE;");
    try {
        const auto encoded = CountVectorCodec::Encode(snapshot.counts);
        sqlite3_stmt* stmt = nullptr;
        CheckSqlite(sqlite3_prepare_v2(db,
                                       "INSERT INTO minute_buckets ("
                                       "bucket_start_utc, keymap_version, counts, total_count, updated_at_utc) "
                                       "VALUES (?1, ?2, ?3, ?4, ?5) "
                                       "ON CONFLICT(bucket_start_utc) DO UPDATE SET "
                                       "keymap_version = excluded.keymap_version, counts = excluded.counts, "
                                       "total_count = excluded.total_count, updated_at_utc = excluded.updated_at_utc;",
                                       -1, &stmt, nullptr),
                    db, "prepare");
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
        sqlite3_bind_int64(stmt, 1, snapshot.bucket_start_utc);
        sqlite3_bind_int(stmt, 2, CountVectorCodec::kKeyMapVersion);
        sqlite3_bind_blob(stmt, 3, encoded.data(), static_cast<int>(encoded.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, snapshot.TotalCount());
        sqlite3_bind_int64(stmt, 5, ToUnixSeconds(std::chrono::system_clock::now()));
        CheckSqlite(sqlite3_step(stmt), db, "step");
        RebuildTenMinuteRollup(db, snapshot.bucket_start_utc);
        Exec(db, "COMMIT;");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

void KeyStatsRepository::AccumulateRows(sqlite3* db, const char* table, long long start_utc, long long end_utc,
                                        std::array<std::uint64_t, kKeyCount>& destination) {
    if (start_utc >= end_utc) {
        return;
    }
    if (std::string_view(table) != "minute_buckets" && std::string_view(table) != "rollup_10m") {
        throw std::invalid_argument("非法表名。");
    }
    const auto sql = std::string("SELECT keymap_version, counts FROM ") + table +
                     " WHERE bucket_start_utc >= ?1 AND bucket_start_utc < ?2;";
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr), db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
    sqlite3_bind_int64(stmt, 1, start_utc);
    sqlite3_bind_int64(stmt, 2, end_utc);
    while (true) {
        const auto rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        CheckSqlite(rc, db, "step");
        ValidateKeyMapVersion(sqlite3_column_int(stmt, 0));
        const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 1));
        const auto bytes = sqlite3_column_bytes(stmt, 1);
        AddCounts(destination, CountVectorCodec::Decode({blob, static_cast<std::size_t>(bytes)}));
    }
}

StatisticsAggregate KeyStatsRepository::QueryAggregate(long long start_utc, long long end_utc) {
    ValidateRange(start_utc, end_utc);
    StatisticsAggregate aggregate{start_utc, end_utc, {}};
    const auto rollup_start = std::min(AlignUp(start_utc, kTenMinuteSeconds), end_utc);
    const auto rollup_end = std::max(AlignDown(end_utc, kTenMinuteSeconds), rollup_start);
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    AccumulateRows(db, "minute_buckets", start_utc, rollup_start, aggregate.counts);
    AccumulateRows(db, "rollup_10m", rollup_start, rollup_end, aggregate.counts);
    AccumulateRows(db, "minute_buckets", rollup_end, end_utc, aggregate.counts);
    return aggregate;
}

std::optional<StoredDataRange> KeyStatsRepository::GetDataRange() {
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db, "SELECT MIN(bucket_start_utc), MAX(bucket_start_utc) FROM minute_buckets;", -1,
                                   &stmt, nullptr),
                db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
    CheckSqlite(sqlite3_step(stmt), db, "step");
    if (sqlite3_column_type(stmt, 0) == SQLITE_NULL || sqlite3_column_type(stmt, 1) == SQLITE_NULL) {
        return std::nullopt;
    }
    return StoredDataRange{sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1) + kMinuteSeconds};
}

std::uint64_t KeyStatsRepository::QueryTotalCount(std::optional<long long> excluded_bucket_start_utc) {
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    sqlite3_stmt* stmt = nullptr;
    if (excluded_bucket_start_utc) {
        CheckSqlite(sqlite3_prepare_v2(db,
                                       "SELECT COALESCE(SUM(total_count), 0) FROM minute_buckets WHERE bucket_start_utc <> ?1;",
                                       -1, &stmt, nullptr),
                    db, "prepare");
        sqlite3_bind_int64(stmt, 1, *excluded_bucket_start_utc);
    } else {
        CheckSqlite(sqlite3_prepare_v2(db, "SELECT COALESCE(SUM(total_count), 0) FROM minute_buckets;", -1, &stmt, nullptr),
                    db, "prepare");
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
    CheckSqlite(sqlite3_step(stmt), db, "step");
    return static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
}

std::vector<TenMinuteBucketTotal> KeyStatsRepository::QueryTenMinuteTotals(long long start_utc, long long end_utc) {
    ValidateRange(start_utc, end_utc);
    std::map<long long, std::uint64_t> totals;
    const auto rollup_start = std::min(AlignUp(start_utc, kTenMinuteSeconds), end_utc);
    const auto rollup_end = std::max(AlignDown(end_utc, kTenMinuteSeconds), rollup_start);
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);

    auto accumulate_minutes = [&](long long from, long long to) {
        if (from >= to) {
            return;
        }
        sqlite3_stmt* stmt = nullptr;
        CheckSqlite(sqlite3_prepare_v2(db,
                                       "SELECT bucket_start_utc, total_count FROM minute_buckets "
                                       "WHERE bucket_start_utc >= ?1 AND bucket_start_utc < ?2;",
                                       -1, &stmt, nullptr),
                    db, "prepare");
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
        sqlite3_bind_int64(stmt, 1, from);
        sqlite3_bind_int64(stmt, 2, to);
        while (true) {
            const auto rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE) {
                break;
            }
            CheckSqlite(rc, db, "step");
            const auto bucket = ToTenMinuteStart(sqlite3_column_int64(stmt, 0));
            totals[bucket] += static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
        }
    };
    auto accumulate_rollups = [&](long long from, long long to) {
        if (from >= to) {
            return;
        }
        sqlite3_stmt* stmt = nullptr;
        CheckSqlite(sqlite3_prepare_v2(db,
                                       "SELECT bucket_start_utc, total_count FROM rollup_10m "
                                       "WHERE bucket_start_utc >= ?1 AND bucket_start_utc < ?2;",
                                       -1, &stmt, nullptr),
                    db, "prepare");
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
        sqlite3_bind_int64(stmt, 1, from);
        sqlite3_bind_int64(stmt, 2, to);
        while (true) {
            const auto rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE) {
                break;
            }
            CheckSqlite(rc, db, "step");
            totals[sqlite3_column_int64(stmt, 0)] += static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
        }
    };

    accumulate_minutes(start_utc, rollup_start);
    accumulate_rollups(rollup_start, rollup_end);
    accumulate_minutes(rollup_end, end_utc);

    std::vector<TenMinuteBucketTotal> series;
    series.reserve(totals.size());
    for (const auto& [bucket, count] : totals) {
        series.push_back({bucket, count});
    }
    return series;
}

void KeyStatsRepository::ExportCsv(long long start_utc, long long end_utc, const std::filesystem::path& destination) {
    ValidateRange(start_utc, end_utc);
    const auto full = std::filesystem::absolute(destination);
    std::filesystem::create_directories(full.parent_path());
    auto temporary = full;
    temporary += ".tmp";
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db,
                                   "SELECT bucket_start_utc, keymap_version, counts FROM minute_buckets "
                                   "WHERE bucket_start_utc >= ?1 AND bucket_start_utc < ?2 ORDER BY bucket_start_utc;",
                                   -1, &stmt, nullptr),
                db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
    sqlite3_bind_int64(stmt, 1, start_utc);
    sqlite3_bind_int64(stmt, 2, end_utc);

    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("无法创建导出文件。");
    }
    stream << "\xEF\xBB\xBF";
    stream << "minute_start_local,minute_start_utc,key_id,key_name,count\n";
    while (true) {
        const auto rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        CheckSqlite(rc, db, "step");
        const auto minute_start = sqlite3_column_int64(stmt, 0);
        ValidateKeyMapVersion(sqlite3_column_int(stmt, 1));
        const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 2));
        const auto bytes = sqlite3_column_bytes(stmt, 2);
        const auto counts = CountVectorCodec::Decode({blob, static_cast<std::size_t>(bytes)});
        const auto local = FormatLocalMinute(minute_start);
        for (int index = 0; index < kKeyCount; ++index) {
            if (counts[static_cast<std::size_t>(index)] == 0) {
                continue;
            }
            const auto& key = KeyCatalog::Get(static_cast<KeyId>(index));
            stream << local << ',' << minute_start << ',' << KeyIdName(key.id) << ','
                   << CsvEscape(key.display_name) << ',' << counts[static_cast<std::size_t>(index)] << '\n';
        }
    }
    stream.flush();
    stream.close();
            std::filesystem::remove(full);
            std::filesystem::rename(temporary, full);
}

std::string KeyStatsRepository::CheckIntegrity() {
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    return ScalarText(db, "PRAGMA integrity_check;");
}

long long KeyStatsRepository::GetStorageSizeBytes() const {
    long long total = 0;
    for (const auto& path : {database_path_, std::filesystem::path(database_path_.wstring() + L"-wal"),
                             std::filesystem::path(database_path_.wstring() + L"-shm")}) {
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            total += static_cast<long long>(std::filesystem::file_size(path, error));
        }
    }
    return total;
}

void KeyStatsRepository::Execute(const std::string& sql) {
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    Exec(db, sql.c_str());
}

void KeyStatsRepository::BindInsertLegacyMinute(long long start, int keymap_version, const std::vector<std::uint8_t>& blob,
                                                long long total) {
    sqlite3* db = Open();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> guard(db, sqlite3_close);
    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db,
                                   "INSERT INTO minute_buckets (bucket_start_utc, keymap_version, counts, total_count, updated_at_utc) "
                                   "VALUES (?1, ?2, ?3, ?4, ?1);",
                                   -1, &stmt, nullptr),
                db, "prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(stmt, sqlite3_finalize);
    sqlite3_bind_int64(stmt, 1, start);
    sqlite3_bind_int(stmt, 2, keymap_version);
    sqlite3_bind_blob(stmt, 3, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, total);
    CheckSqlite(sqlite3_step(stmt), db, "step");
}

}  // namespace keystats
