#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::network {

// 阶段9 指令七：协议版本（Client/Server 握手时验证，不一致拒绝连接）
inline constexpr std::uint16_t kProtocolVersion = 1;

// 阶段9 指令九：Packet magic 'LGDN'（所有收到的包先验证，错误立即断开）
inline constexpr std::uint32_t kPacketMagic = 0x4C47444Eu;

// 阶段9 指令八：固定长度 Header 总字节数（magic4 + version2 + messageId2 +
// payloadSize4 + sequence4，手动大端序列化，禁止直接 memcpy 结构体）
inline constexpr std::size_t kPacketHeaderSize = 16;

// 阶段9 指令十一：payload 长度上限（防恶意/损坏 Header 触发超大内存分配）
inline constexpr std::uint32_t kMaxPacketPayload = 64u * 1024u;

// 阶段9 指令十五：String 字段最大字节数（Writer 编码超限失败、Reader 越界失败）
inline constexpr std::uint16_t kMaxStringLength = 4096;

} // namespace legend::network
