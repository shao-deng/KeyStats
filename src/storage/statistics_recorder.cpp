#include "statistics_recorder.hpp"

#include "repository.hpp"
#include "time_buckets.hpp"

#include <stdexcept>
#include <utility>

namespace keystats {

StatisticsRecorder::StatisticsRecorder(KeyStatsRepository& repository, IUtcClock& clock,
                                       std::chrono::seconds flush_interval, MinuteBucketSnapshot initial)
    : repository_(repository),
      clock_(clock),
      flush_interval_(flush_interval),
      current_minute_start_(initial.bucket_start_utc),
      current_counts_(initial.counts) {
    save_thread_ = std::thread([this] { ProcessQueue(); });
    timer_thread_ = std::thread([this] { PeriodicFlush(); });
}

std::unique_ptr<StatisticsRecorder> StatisticsRecorder::Create(
    KeyStatsRepository& repository, std::chrono::seconds flush_interval, IUtcClock* clock) {
    if (flush_interval < std::chrono::seconds(1)) {
        throw std::invalid_argument("flush_interval");
    }
    static SystemUtcClock system_clock;
    auto& resolved = clock ? *clock : static_cast<IUtcClock&>(system_clock);
    const auto current_minute = ToMinuteStartUnix(ToUnixSeconds(resolved.UtcNow()));
    auto initial = repository.GetMinuteBucket(current_minute).value_or(MinuteBucketSnapshot{current_minute, {}});
    return std::unique_ptr<StatisticsRecorder>(new StatisticsRecorder(repository, resolved, flush_interval, initial));
}

StatisticsRecorder::~StatisticsRecorder() {
    disposed_ = 1;
    {
        std::lock_guard lock(queue_gate_);
        stop_timer_ = true;
    }
    try {
        QueueCurrentAndWait(true);
    } catch (...) {
    }
    {
        std::lock_guard lock(queue_gate_);
        stop_queue_ = true;
    }
    queue_cv_.notify_all();
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }
    if (save_thread_.joinable()) {
        save_thread_.join();
    }
}

void StatisticsRecorder::RecordKeyPress(KeyId key_id) {
    if (disposed_ != 0) {
        return;
    }
    std::optional<MinuteBucketSnapshot> completed;
    {
        std::lock_guard lock(bucket_gate_);
        const auto minute_start = ToMinuteStartUnix(ToUnixSeconds(clock_.UtcNow()));
        if (minute_start != current_minute_start_) {
            completed = SnapshotLocked();
            current_minute_start_ = minute_start;
            current_counts_ = {};
        }
        auto& count = current_counts_[static_cast<std::size_t>(ToIndex(key_id))];
        if (count < UINT32_MAX) {
            ++count;
        }
    }
    if (completed && completed->TotalCount() > 0) {
        std::lock_guard lock(queue_gate_);
        queue_.push(SaveCommand{*completed, 0});
        queue_cv_.notify_one();
    }
}

MinuteBucketSnapshot StatisticsRecorder::GetCurrentSnapshot() {
    std::lock_guard lock(bucket_gate_);
    return SnapshotLocked();
}

void StatisticsRecorder::FlushNow() {
    QueueCurrentAndWait(false);
}

std::optional<long long> StatisticsRecorder::LastSavedUnixSeconds() const {
    std::lock_guard lock(queue_gate_);
    if (last_saved_unix_seconds_ == 0) {
        return std::nullopt;
    }
    return last_saved_unix_seconds_;
}

std::string StatisticsRecorder::LastError() const {
    std::lock_guard lock(queue_gate_);
    return last_error_;
}

MinuteBucketSnapshot StatisticsRecorder::SnapshotLocked() const {
    return MinuteBucketSnapshot{current_minute_start_, current_counts_};
}

void StatisticsRecorder::QueueCurrentAndWait(bool allow_disposed) {
    if (!allow_disposed && disposed_ != 0) {
        throw std::runtime_error("StatisticsRecorder 已释放。");
    }
    MinuteBucketSnapshot snapshot;
    {
        std::lock_guard lock(bucket_gate_);
        snapshot = SnapshotLocked();
    }
    if (snapshot.TotalCount() == 0) {
        return;
    }
    int wait_id = 0;
    {
        std::lock_guard lock(queue_gate_);
        wait_id = next_wait_id_++;
        wait_failed_ = false;
        wait_error_.clear();
        queue_.push(SaveCommand{snapshot, wait_id});
    }
    queue_cv_.notify_one();
    std::unique_lock lock(queue_gate_);
    wait_cv_.wait(lock, [&] { return completed_wait_id_ >= wait_id || stop_queue_; });
    if (wait_failed_) {
        throw std::runtime_error(wait_error_.empty() ? "保存失败。" : wait_error_);
    }
}

void StatisticsRecorder::ProcessQueue() {
    while (true) {
        SaveCommand command;
        {
            std::unique_lock lock(queue_gate_);
            queue_cv_.wait(lock, [&] { return stop_queue_ || !queue_.empty(); });
            if (stop_queue_ && queue_.empty()) {
                break;
            }
            command = std::move(queue_.front());
            queue_.pop();
        }
        try {
            repository_.UpsertMinuteBucket(command.snapshot);
            std::lock_guard lock(queue_gate_);
            last_saved_unix_seconds_ = ToUnixSeconds(clock_.UtcNow());
            last_error_.clear();
            if (command.wait_id != 0) {
                completed_wait_id_ = command.wait_id;
                wait_failed_ = false;
                wait_cv_.notify_all();
            }
        } catch (const std::exception& exception) {
            std::lock_guard lock(queue_gate_);
            last_error_ = exception.what();
            if (command.wait_id != 0) {
                completed_wait_id_ = command.wait_id;
                wait_failed_ = true;
                wait_error_ = exception.what();
                wait_cv_.notify_all();
            }
        }
    }
}

void StatisticsRecorder::PeriodicFlush() {
    const auto slice = std::chrono::milliseconds(100);
    auto elapsed = std::chrono::milliseconds(0);
    while (true) {
        {
            std::lock_guard lock(queue_gate_);
            if (stop_timer_) {
                break;
            }
        }
        std::this_thread::sleep_for(slice);
        elapsed += slice;
        if (elapsed < flush_interval_) {
            continue;
        }
        elapsed = std::chrono::milliseconds(0);
        MinuteBucketSnapshot snapshot;
        {
            std::lock_guard lock(bucket_gate_);
            snapshot = SnapshotLocked();
        }
        if (snapshot.TotalCount() > 0) {
            std::lock_guard lock(queue_gate_);
            queue_.push(SaveCommand{snapshot, 0});
            queue_cv_.notify_one();
        }
    }
}

}  // namespace keystats
