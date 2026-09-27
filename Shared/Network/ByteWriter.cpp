#include "Shared/Network/ByteWriter.h"

#include "Shared/Network/NetworkConstants.h"

namespace legend::network {

ByteWriter::ByteWriter(std::vector<std::uint8_t>& buffer) : m_buffer(buffer) {}

void ByteWriter::WriteUInt8(std::uint8_t value) {
    m_buffer.push_back(value);
}

void ByteWriter::WriteUInt16(std::uint16_t value) {
    m_buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    m_buffer.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void ByteWriter::WriteUInt32(std::uint32_t value) {
    m_buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    m_buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    m_buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    m_buffer.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void ByteWriter::WriteUInt64(std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        m_buffer.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void ByteWriter::WriteInt32(std::int32_t value) {
    WriteUInt32(static_cast<std::uint32_t>(value));
}

void ByteWriter::WriteFloat(float value) {
    // IEEE-754 位型拷贝（避免类型双关 UB），再按 big endian 序列化
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    const auto* src = reinterpret_cast<const unsigned char*>(&value);
    for (int i = 0; i < 4; ++i) {
        bits = (bits << 8) | static_cast<std::uint32_t>(src[i]);
    }
    WriteUInt32(bits);
}

void ByteWriter::WriteBool(bool value) {
    WriteUInt8(value ? 1u : 0u);
}

bool ByteWriter::WriteString(const std::string& value) {
    if (value.size() > kMaxStringLength) {
        return false; // 指令十五：超长失败，不写入任何字节
    }
    WriteUInt16(static_cast<std::uint16_t>(value.size()));
    m_buffer.insert(m_buffer.end(), value.begin(), value.end());
    return true;
}

} // namespace legend::network
