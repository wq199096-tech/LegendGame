#pragma once

#include "Shared/Network/NetworkConstants.h"
#include "Shared/Network/PacketHeader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::network {

// Packet 结构体定义在 PacketHeader.h（本头只提供编解码）。

// 阶段9 指令十八：PacketCodec 只负责编解码（Header/Packet <-> 字节流），
// 不碰 Socket、不处理业务。全部 big endian（指令十）。
class PacketCodec {
public:
    // Header -> 16 字节 big endian（写入 out 尾部）
    static void EncodeHeader(const PacketHeader& header, std::vector<std::uint8_t>& out);
    // 从 data 解码 Header：必须至少 kPacketHeaderSize 字节；验证 magic +
    // payloadSize 上限（指令九/十一）。失败返回 false 且 error 给出原因。
    static bool DecodeHeader(const std::uint8_t* data, std::size_t size,
                             PacketHeader& out, std::string& error);
    // Header + payload -> 完整字节流（Header 自动 big endian 编码）
    static void EncodePacket(const Packet& packet, std::vector<std::uint8_t>& out);
    // 完整字节流 -> Packet：验证 magic / version / payloadSize / 实际长度
    static bool DecodePacket(const std::uint8_t* data, std::size_t size,
                             Packet& out, std::string& error);
};

} // namespace legend::network
