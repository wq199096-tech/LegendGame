#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace legend::network {

// 阶段9 指令十六/十七：ByteReader——全部接口边界安全，越界即进入 failed
// 状态（粘滞失败：后续读取继续返回失败值），绝不越过 buffer。
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size);

    std::uint8_t ReadUInt8();
    std::uint16_t ReadUInt16(); // big endian
    std::uint32_t ReadUInt32(); // big endian
    std::uint64_t ReadUInt64(); // big endian
    std::int32_t ReadInt32();
    float ReadFloat();
    bool ReadBool();
    // uint16 长度 + 字节；越界或长度超过剩余字节 -> 失败（返回 false）
    bool ReadString(std::string& out);

    bool IsValid() const { return m_valid; }
    std::size_t Offset() const { return m_offset; }
    std::size_t Remaining() const { return m_valid ? m_size - m_offset : 0; }
    // 手动进入粘滞失败状态（语义校验失败时使用，如 batch count 超上限）
    void Invalidate() { m_valid = false; }

private:
    // 取 n 字节指针；越界 -> 置 failed 并返回 nullptr
    const std::uint8_t* Take(std::size_t count);

    const std::uint8_t* m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_offset = 0;
    bool m_valid = true;
};

} // namespace legend::network
