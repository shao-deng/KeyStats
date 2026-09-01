#include "../src/core/key_catalog.hpp"
#include "../src/core/key_normalizer.hpp"
#include "../src/core/keyboard_counter.hpp"
#include "../src/storage/count_vector_codec.hpp"
#include "../src/storage/repository.hpp"
#include "../src/storage/settings_store.hpp"
#include "../src/storage/statistics_recorder.hpp"
#include "../src/storage/time_buckets.hpp"

#include <array>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace keystats;

namespace {

int g_failures = 0;
int g_passed = 0;

void Fail(const std::string& name, const std::string& message) {
    ++g_failures;
    std::cout << "FAIL  " << name << "\n      " << message << "\n";
}

void Pass(const std::string& name) {
    ++g_passed;
    std::cout << "PASS  " << name << "\n";
}

template <typename T>
void Equal(const T& expected, const T& actual, const char* context) {
    if (expected != actual) {
        throw std::runtime_error(std::string(context) + " 期望与实际不一致。");
    }
}

std::optional<KeyId> Normalize(std::uint16_t scan, std::uint16_t vk, RawKeyFlags flags = RawKeyFlags::None) {
    return KeyNormalizer::Normalize(RawKeyEvent{scan, flags, vk});
}

RawKeyEvent MakeA() { return RawKeyEvent{0x1E, RawKeyFlags::None, 0x41}; }
RawKeyEvent BreakA() { return RawKeyEvent{0x1E, RawKeyFlags::Break, 0x41}; }

std::array<std::uint32_t, kKeyCount> NewCounts(std::initializer_list<std::pair<KeyId, std::uint32_t>> values) {
    std::array<std::uint32_t, kKeyCount> counts{};
    for (const auto& [key, count] : values) {
        counts[static_cast<std::size_t>(ToIndex(key))] = count;
    }
    return counts;
}

std::filesystem::path CreateTestDirectory() {
    auto directory = std::filesystem::temp_directory_path() / "KeyStats.Tests" / std::to_string(std::rand());
    std::filesystem::create_directories(directory);
    return directory;
}

class ManualClock final : public IUtcClock {
public:
    explicit ManualClock(long long unix_seconds)
        : now_(std::chrono::system_clock::time_point(std::chrono::seconds(unix_seconds))) {}

    std::chrono::system_clock::time_point UtcNow() const override { return now_; }

private:
    std::chrono::system_clock::time_point now_;
};

template <typename Fn>
void WithRepository(Fn&& fn) {
    const auto directory = CreateTestDirectory();
    try {
        KeyStatsRepository repository(directory / "test.db");
        repository.Initialize();
        fn(directory, repository);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        throw;
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

void Run(const std::string& name, const std::function<void()>& fn) {
    try {
        fn();
        Pass(name);
    } catch (const std::exception& exception) {
        Fail(name, exception.what());
    }
}

}  // namespace

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    Run("标准字母按扫描码映射", [] {
        Equal(KeyId::A, *Normalize(0x1E, 0x41), "A");
        Equal(KeyId::Q, *Normalize(0x10, 0x51), "Q");
        Equal(KeyId::Space, *Normalize(0x39, 0x20), "Space");
    });
    Run("左右修饰键分离", [] {
        Equal(KeyId::LeftShift, *Normalize(0x2A, 0x10), "LShift");
        Equal(KeyId::RightShift, *Normalize(0x36, 0x10), "RShift");
        Equal(KeyId::LeftControl, *Normalize(0x1D, 0x11), "LCtrl");
        Equal(KeyId::RightControl, *Normalize(0x1D, 0x11, RawKeyFlags::E0), "RCtrl");
        Equal(KeyId::LeftAlt, *Normalize(0x38, 0x12), "LAlt");
        Equal(KeyId::RightAlt, *Normalize(0x38, 0x12, RawKeyFlags::E0), "RAlt");
    });
    Run("主 Enter 与数字键盘 Enter 分离", [] {
        Equal(KeyId::Enter, *Normalize(0x1C, 0x0D), "Enter");
        Equal(KeyId::NumpadEnter, *Normalize(0x1C, 0x0D, RawKeyFlags::E0), "NumEnter");
    });
    Run("导航键与数字键盘分离", [] {
        Equal(KeyId::Numpad7, *Normalize(0x47, 0x67), "Num7");
        Equal(KeyId::Home, *Normalize(0x47, 0x24, RawKeyFlags::E0), "Home");
        Equal(KeyId::NumpadDecimal, *Normalize(0x53, 0x6E), "NumDot");
        Equal(KeyId::Delete, *Normalize(0x53, 0x2E, RawKeyFlags::E0), "Delete");
    });
    Run("F13 到 F24 映射", [] {
        Equal(KeyId::F13, *Normalize(0, 0x7C), "F13");
        Equal(KeyId::F24, *Normalize(0, 0x87), "F24");
    });
    Run("媒体键回退映射", [] {
        Equal(KeyId::MediaPlayPause, *Normalize(0, 0xB3, RawKeyFlags::E0), "Play");
        Equal(KeyId::VolumeUp, *Normalize(0, 0xAF, RawKeyFlags::E0), "VolUp");
    });
    Run("媒体虚拟键优先于冲突扫描码", [] {
        Equal(KeyId::VolumeMute, *Normalize(0x22, 0xAD, RawKeyFlags::E0), "Mute");
    });
    Run("长按自动连发只计一次", [] {
        KeyboardCounter counter;
        counter.Process(1, MakeA());
        counter.Process(1, MakeA());
        counter.Process(1, MakeA());
        Equal(1LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "count");
    });
    Run("松开后再次按下重新计数", [] {
        KeyboardCounter counter;
        counter.Process(1, MakeA());
        counter.Process(1, BreakA());
        counter.Process(1, MakeA());
        const auto snapshot = counter.GetSnapshot();
        Equal(2LL, snapshot.counts[ToIndex(KeyId::A)], "A");
        Equal(2LL, snapshot.total_count, "total");
    });
    Run("两把键盘分别按下会各计一次", [] {
        KeyboardCounter counter;
        counter.Process(1, MakeA());
        counter.Process(2, MakeA());
        Equal(2LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "two devices");
    });
    Run("组合键中的每个物理键分别计数", [] {
        KeyboardCounter counter;
        counter.Process(1, RawKeyEvent{0x1D, RawKeyFlags::None, 0x11});
        counter.Process(1, RawKeyEvent{0x2E, RawKeyFlags::None, 0x43});
        counter.Process(1, RawKeyEvent{0x2E, RawKeyFlags::Break, 0x43});
        counter.Process(1, RawKeyEvent{0x2F, RawKeyFlags::None, 0x56});
        counter.Process(1, RawKeyEvent{0x2F, RawKeyFlags::Break, 0x56});
        counter.Process(1, RawKeyEvent{0x1D, RawKeyFlags::Break, 0x11});
        const auto snapshot = counter.GetSnapshot();
        Equal(1LL, snapshot.counts[ToIndex(KeyId::LeftControl)], "ctrl");
        Equal(1LL, snapshot.counts[ToIndex(KeyId::C)], "C");
        Equal(1LL, snapshot.counts[ToIndex(KeyId::V)], "V");
        Equal(3LL, snapshot.total_count, "total");
    });
    Run("系统切换丢失松开消息后可恢复计数", [] {
        KeyboardCounter counter;
        const RawKeyEvent alt_down{0x38, RawKeyFlags::None, 0x12};
        const RawKeyEvent tab_down{0x0F, RawKeyFlags::None, 0x09};
        counter.Process(1, alt_down);
        counter.Process(1, tab_down);
        counter.ReleaseKey(KeyId::LeftAlt);
        counter.ReleaseKey(KeyId::Tab);
        counter.Process(1, alt_down);
        counter.Process(1, tab_down);
        const auto snapshot = counter.GetSnapshot();
        Equal(2LL, snapshot.counts[ToIndex(KeyId::LeftAlt)], "alt");
        Equal(2LL, snapshot.counts[ToIndex(KeyId::Tab)], "tab");
    });
    Run("Alt+Tab 补偿与 Raw Input 不会重复计数", [] {
        KeyboardCounter counter;
        const RawKeyEvent tab_down{0x0F, RawKeyFlags::None, 0x09};
        const RawKeyEvent tab_up{0x0F, RawKeyFlags::Break, 0x09};
        Equal(true, counter.ProcessButton(0, KeyId::Tab, false).counted, "first");
        Equal(false, counter.ProcessDetailed(7, tab_down).counted, "dup");
        counter.ProcessDetailed(7, tab_up);
        Equal(true, counter.ProcessDetailed(7, tab_down).counted, "second");
        Equal(false, counter.ProcessButton(0, KeyId::Tab, false).counted, "dup2");
        Equal(2LL, counter.GetSnapshot().counts[ToIndex(KeyId::Tab)], "tab total");
    });
    Run("鼠标左右键分别计数且按住不连发", [] {
        KeyboardCounter counter;
        Equal(true, counter.ProcessButton(9, KeyId::MouseLeftButton, false).counted, "left");
        Equal(false, counter.ProcessButton(9, KeyId::MouseLeftButton, false).counted, "repeat");
        counter.ProcessButton(9, KeyId::MouseRightButton, false);
        auto held = counter.GetSnapshot();
        Equal(1LL, held.counts[ToIndex(KeyId::MouseLeftButton)], "left count");
        Equal(1LL, held.counts[ToIndex(KeyId::MouseRightButton)], "right count");
        Equal(true, held.IsPressed(KeyId::MouseLeftButton), "left pressed");
        Equal(true, held.IsPressed(KeyId::MouseRightButton), "right pressed");
        counter.ProcessButton(9, KeyId::MouseLeftButton, true);
        counter.ProcessButton(9, KeyId::MouseLeftButton, false);
        Equal(2LL, counter.GetSnapshot().counts[ToIndex(KeyId::MouseLeftButton)], "left after release");
    });
    Run("暂停期间不计数且恢复不误计长按", [] {
        KeyboardCounter counter;
        counter.Process(1, MakeA());
        counter.SetPaused(true);
        counter.Process(1, BreakA());
        counter.Process(1, MakeA());
        counter.SetPaused(false);
        counter.Process(1, MakeA());
        Equal(1LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "paused");
        counter.Process(1, BreakA());
        counter.Process(1, MakeA());
        Equal(2LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "resumed");
    });
    Run("清零不破坏按下状态", [] {
        KeyboardCounter counter;
        counter.Process(1, MakeA());
        counter.ClearCounts();
        counter.Process(1, MakeA());
        Equal(0LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "still held");
        counter.Process(1, BreakA());
        counter.Process(1, MakeA());
        Equal(1LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "after release");
    });
    Run("界面清零可重置残留按下状态", [] {
        KeyboardCounter counter;
        counter.Process(1, MakeA());
        counter.ClearCounts(true);
        counter.Process(1, MakeA());
        Equal(1LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "reset");
    });
    Run("设备移除会清理按下状态", [] {
        KeyboardCounter counter;
        counter.Process(17, MakeA());
        counter.RemoveDevice(17);
        counter.Process(17, MakeA());
        Equal(2LL, counter.GetSnapshot().counts[ToIndex(KeyId::A)], "removed");
    });
    Run("未知键不会写入计数", [] {
        KeyboardCounter counter;
        Equal(false, counter.Process(1, RawKeyEvent{0x7F, RawKeyFlags::None, 0xFE}), "unknown");
        Equal(0LL, counter.GetSnapshot().total_count, "total");
    });
    Run("稀疏计数向量往返一致", [] {
        auto counts = NewCounts({{KeyId::A, 17}, {KeyId::Space, 43}, {KeyId::NumpadEnter, 2}});
        const auto encoded = CountVectorCodec::Encode(counts);
        const auto decoded = CountVectorCodec::Decode(encoded);
        Equal(counts, decoded, "roundtrip");
    });
    Run("阶段 2 的 Key Map v1 数据仍可读取", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            constexpr long long minute_start = 1'800'000'000;
            const auto blob = CountVectorCodec::Encode(NewCounts({{KeyId::C, 4}, {KeyId::LeftControl, 2}}));
            repository.BindInsertLegacyMinute(minute_start, 1, blob, 6);
            const auto aggregate = repository.QueryAggregate(minute_start, minute_start + 60);
            Equal(4ULL, aggregate.counts[ToIndex(KeyId::C)], "C");
            Equal(2ULL, aggregate.counts[ToIndex(KeyId::LeftControl)], "Ctrl");
            Equal(0ULL, aggregate.counts[ToIndex(KeyId::MouseLeftButton)], "mouse");
        });
    });
    Run("数据库一分钟写入与绝对快照更新", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            constexpr long long minute_start = 1'800'000'000;
            repository.UpsertMinuteBucket({minute_start, NewCounts({{KeyId::A, 3}})});
            const auto loaded = repository.GetMinuteBucket(minute_start);
            Equal(3U, loaded->counts[ToIndex(KeyId::A)], "loaded");
            repository.UpsertMinuteBucket({minute_start, NewCounts({{KeyId::A, 5}})});
            const auto aggregate = repository.QueryAggregate(minute_start, minute_start + 60);
            Equal(5ULL, aggregate.TotalCount(), "total");
            Equal(std::string("ok"), repository.CheckIntegrity(), "integrity");
        });
    });
    Run("全部范围边界覆盖首末有效分钟", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            constexpr long long first = 1'800'000'000;
            constexpr long long last = first + 3600;
            repository.UpsertMinuteBucket({first, NewCounts({{KeyId::A, 1}})});
            repository.UpsertMinuteBucket({last, NewCounts({{KeyId::MouseLeftButton, 2}})});
            const auto range = repository.GetDataRange();
            Equal(first, range->start_utc, "start");
            Equal(last + 60, range->end_utc, "end");
        });
    });
    Run("历史总数量可排除当前分钟", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            constexpr long long first = 1'800'000'000;
            constexpr long long current = first + 60;
            repository.UpsertMinuteBucket({first, NewCounts({{KeyId::A, 2}})});
            repository.UpsertMinuteBucket({current, NewCounts({{KeyId::W, 3}})});
            Equal(5ULL, repository.QueryTotalCount(), "all");
            Equal(2ULL, repository.QueryTotalCount(current), "exclude");
        });
    });
    Run("十分钟派生汇总正确", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            constexpr long long start = 1'800'000'000;
            repository.UpsertMinuteBucket({start, NewCounts({{KeyId::A, 2}})});
            repository.UpsertMinuteBucket({start + 60, NewCounts({{KeyId::B, 3}})});
            const auto aggregate = repository.QueryAggregate(start, start + 600);
            Equal(5ULL, aggregate.TotalCount(), "total");
            Equal(2ULL, aggregate.counts[ToIndex(KeyId::A)], "A");
            Equal(3ULL, aggregate.counts[ToIndex(KeyId::B)], "B");
        });
    });
    Run("十分钟时段序列正确处理范围边缘", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            constexpr long long base = 1'800'000'000;
            repository.UpsertMinuteBucket({base + 60, NewCounts({{KeyId::A, 1}})});
            repository.UpsertMinuteBucket({base + 600, NewCounts({{KeyId::B, 2}})});
            repository.UpsertMinuteBucket({base + 1200, NewCounts({{KeyId::C, 3}})});
            const auto series = repository.QueryTenMinuteTotals(base + 60, base + 1260);
            Equal(3, static_cast<int>(series.size()), "size");
            Equal(base, series[0].bucket_start_utc, "b0");
            Equal(1ULL, series[0].total_count, "c0");
            Equal(base + 600, series[1].bucket_start_utc, "b1");
            Equal(2ULL, series[1].total_count, "c1");
            Equal(base + 1200, series[2].bucket_start_utc, "b2");
            Equal(3ULL, series[2].total_count, "c2");
        });
    });
    Run("同一分钟重启后继续累计", [] {
        WithRepository([](const auto&, KeyStatsRepository& repository) {
            ManualClock clock(1'800'000'030);
            {
                auto recorder = StatisticsRecorder::Create(repository, std::chrono::hours(1), &clock);
                recorder->RecordKeyPress(KeyId::A);
                recorder->FlushNow();
            }
            {
                auto recorder = StatisticsRecorder::Create(repository, std::chrono::hours(1), &clock);
                recorder->RecordKeyPress(KeyId::A);
                recorder->FlushNow();
            }
            const auto minute_start = ToMinuteStartUnix(ToUnixSeconds(clock.UtcNow()));
            const auto aggregate = repository.QueryAggregate(minute_start, minute_start + 60);
            Equal(2ULL, aggregate.TotalCount(), "resume");
        });
    });
    Run("CSV 导出包含分钟与键位", [] {
        WithRepository([](const auto& directory, KeyStatsRepository& repository) {
            constexpr long long minute_start = 1'800'000'000;
            repository.UpsertMinuteBucket({minute_start, NewCounts({{KeyId::Space, 4}})});
            const auto csv_path = directory / "export.csv";
            repository.ExportCsv(minute_start, minute_start + 60, csv_path);
            std::ifstream stream(csv_path);
            const std::string csv((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            if (csv.find("minute_start_local") == std::string::npos || csv.find("Space") == std::string::npos ||
                csv.find(",4") == std::string::npos) {
                throw std::runtime_error("没有找到期望文本。");
            }
        });
    });
    Run("设置文件可原子创建和读取", [] {
        const auto directory = CreateTestDirectory();
        try {
            SettingsStore store((directory / "settings.json").string());
            auto settings = store.LoadOrCreate();
            Equal(30, settings.flush_interval_seconds, "default");
            settings.flush_interval_seconds = 45;
            store.Save(settings);
            Equal(45, store.LoadOrCreate().flush_interval_seconds, "saved");
        } catch (...) {
            std::filesystem::remove_all(directory);
            throw;
        }
        std::filesystem::remove_all(directory);
    });

    std::cout << "\n结果：" << g_passed << "/" << (g_passed + g_failures) << " 通过\n";
    return g_failures == 0 ? 0 : 1;
}
