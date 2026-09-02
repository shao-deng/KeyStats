#include "count_vector_codec.hpp"

#include <stdexcept>
#include <string>

namespace keystats {
namespace {

constexpr int kHeaderSize = 3;
constexpr int kEntrySize = 6;

void WriteU16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void WriteU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::uint16_t ReadU16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t ReadU32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) |
                                      (data[offset + 3] << 24));
}

}  // namespace

std::vector<std::uint8_t> CountVectorCodec::Encode(std::span<const std::uint32_t> counts) {
    if (static_cast<int>(counts.size()) != kKeyCount) {
        throw std::invalid_argument("计数向量长度应为 " + std::to_string(kKeyCount) + "。");
    }
    int non_zero = 0;
    for (const auto count : counts) {
        if (count != 0) {
            ++non_zero;
        }
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(kHeaderSize + non_zero * kEntrySize));
    data[0] = kFormatVersion;
    WriteU16(data, 1, static_cast<std::uint16_t>(non_zero));
    auto offset = static_cast<std::size_t>(kHeaderSize);
    for (int key_index = 0; key_index < static_cast<int>(counts.size()); ++key_index) {
        const auto count = counts[static_cast<std::size_t>(key_index)];
        if (count == 0) {
            continue;
        }
        WriteU16(data, offset, static_cast<std::uint16_t>(key_index));
        WriteU32(data, offset + 2, count);
        offset += kEntrySize;
    }
    return data;
}

std::array<std::uint32_t, kKeyCount> CountVectorCodec::Decode(std::span<const std::uint8_t> data) {
    if (data.size() < kHeaderSize) {
        throw std::runtime_error("计数向量过短。");
    }
    if (data[0] != kFormatVersion) {
        throw std::runtime_error("不支持的计数向量版本。");
    }
    const auto entry_count = ReadU16(data, 1);
    const auto expected = static_cast<std::size_t>(kHeaderSize + entry_count * kEntrySize);
    if (data.size() != expected) {
        throw std::runtime_error("计数向量长度不正确。");
    }
    std::array<std::uint32_t, kKeyCount> counts{};
    std::array<bool, kKeyCount> seen{};
    auto offset = static_cast<std::size_t>(kHeaderSize);
    for (int entry = 0; entry < entry_count; ++entry) {
        const auto key_index = ReadU16(data, offset);
        if (key_index >= counts.size() || seen[key_index]) {
            throw std::runtime_error("计数向量包含无效或重复 Key ID。");
        }
        const auto count = ReadU32(data, offset + 2);
        if (count == 0) {
            throw std::runtime_error("稀疏计数向量不应包含零值。");
        }
        counts[key_index] = count;
        seen[key_index] = true;
        offset += kEntrySize;
    }
    return counts;
}

}  // namespace keystats
