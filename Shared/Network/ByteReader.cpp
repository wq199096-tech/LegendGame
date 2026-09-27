#include "Shared/Network/ByteReader.h"

#include <bit>

namespace legend::network {

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size)
    : m_data(data), m_size(size) {}

const std::uint8_t* ByteReader::Take(std::size_t count) {
    if (!m_valid || m_offset + count > m_size) {
        m_valid = false; // 指令十七：粘滞失败
        return nullptr;
    }
    const std::uint8_t* result = m_data + m_offset;
    m_offset += count;
    return result;
}

std::uint8_t ByteReader::ReadUInt8() {
    const auto* p = Take(1);
    return p != nullptr ? p[0] : std::uint8_t{0};
}

std::uint16_t ByteReader::ReadUInt16() {
    const auto* p = Take(2);
    if (p == nullptr) {
        return 0;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t ByteReader::ReadUInt32() {
    const auto* p = Take(4);
    if (p == nullptr) {
        return 0;
    }
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint64_t ByteReader::ReadUInt64() {
    const auto* p = Take(8);
    if (p == nullptr) {
        return 0;
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return value;
}

std::int32_t ByteReader::ReadInt32() {
    return static_cast<std::int32_t>(ReadUInt32());
}

float ByteReader::ReadFloat() {
    // 阶段9.1指令二十一：bit_cast 还原 IEEE-754
    return std::bit_cast<float>(ReadUInt32());
}

bool ByteReader::ReadBool() {
    return ReadUInt8() != 0;
}

bool ByteReader::ReadString(std::string& out) {
    if (!m_valid) {
        return false;
    }
    const std::size_t savedOffset = m_offset;
    const std::uint16_t length = ReadUInt16();
    if (!m_valid) {
        m_offset = savedOffset;
        return false;
    }
    // 指令九十六：恶意长度（如 65535）但 buffer 只剩 2 字节 -> 失败，不分配
    const auto* p = Take(length);
    if (p == nullptr) {
        m_offset = savedOffset;
        return false;
    }
    out.assign(reinterpret_cast<const char*>(p), length);
    return true;
}

} // namespace legend::network
