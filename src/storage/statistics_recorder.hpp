#pragma once

#include "../core/key_id.hpp"
#include "storage_types.hpp"
#include "utc_clock.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace keystats {

class KeyStatsRepository;

class StatisticsRecorder {
public:
    static std::unique_ptr<StatisticsRecorder> Create(
        KeyStatsRepository& repository,
        std::chrono::seconds flush_interval,
        IUtcClock* clock = nullptr);

    ~StatisticsRecorder();

    StatisticsRecorder(const StatisticsRecorder&) = delete;
    StatisticsRecorder& operator=(const StatisticsRecorder&) = delete;

    void RecordKeyPress(KeyId key_id);
    MinuteBucketSnapshot GetCurrentSnapshot();
    void FlushNow();

    std::optional<long long> LastSavedUnixSeconds() const;
    std::string LastError() const;

private:
    struct SaveCommand {
        MinuteBucketSnapshot snapshot;
        int wait_id = 0;
    };

    StatisticsRecorder(KeyStatsRepository& repository, IUtcClock& clock, std::chrono::seconds flush_interval,
                       MinuteBucketSnapshot initial);

    MinuteBucketSnapshot SnapshotLocked() const;
    void ProcessQueue();
    void PeriodicFlush();
    void QueueCurrentAndWait(bool allow_disposed);

    KeyStatsRepository& repository_;
    IUtcClock& clock_;
    std::chrono::seconds flush_interval_;
    mutable std::mutex bucket_gate_;
    std::mutex queue_gate_;
    std::condition_variable queue_cv_;
    std::condition_variable wait_cv_;
    std::queue<SaveCommand> queue_;
    std::thread save_thread_;
    std::thread timer_thread_;
    long long current_minute_start_ = 0;
    std::array<std::uint32_t, kKeyCount> current_counts_{};
    long long last_saved_unix_seconds_ = 0;
    std::string last_error_;
    int next_wait_id_ = 1;
    int completed_wait_id_ = 0;
    bool wait_failed_ = false;
    std::string wait_error_;
    bool stop_timer_ = false;
    bool stop_queue_ = false;
    int disposed_ = 0;
};

}  // namespace keystats
