#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::network {

// 阶段9 指令十四：ByteWriter——手动大端序列化（禁止 memcpy 结构体）。
// String 格式：uint16 长度 + UTF-8 字节（指令十五：超长失败）。
class ByteWriter {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& buffer);

    void WriteUInt8(std::uint8_t value);
    void WriteUInt16(std::uint16_t value); // big endian
    void WriteUInt32(std::uint32_t value); // big endian
    void WriteUInt64(std::uint64_t value); // big endian
    void WriteInt32(std::int32_t value);
    void WriteFloat(float value);          // IEEE-754 bits，big endian
    void WriteBool(bool value);            // 1 字节 0/1
    // 指令十五：超过 kMaxStringLength 返回 false（不写入任何字节）
    bool WriteString(const std::string& value);

private:
    std::vector<std::uint8_t>& m_buffer;
};

} // namespace legend::network
