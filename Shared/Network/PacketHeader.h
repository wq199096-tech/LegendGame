#pragma once

#include "Shared/Network/NetworkConstants.h"

#include <cstdint>
#include <vector>

namespace legend::network {

// 阶段9 指令八：固定长度 Packet Header（16 字节，手动序列化——禁止直接
// send(sizeof(struct))，结构体 padding/endian 不同平台不可靠）。
struct PacketHeader {
    std::uint32_t magic = kPacketMagic;     // 'LGDN'，收包先验证，错误立即断开
    std::uint16_t version = kProtocolVersion; // 协议版本，握手时验证
    std::uint16_t messageId = 0;            // MessageId 序号
    std::uint32_t payloadSize = 0;          // payload 字节数（<= kMaxPacketPayload）
    std::uint32_t sequence = 0;             // 每连接发送序号 1,2,3...（仅 Debug/异常检测）
};

// 阶段9 指令十九：完整 Packet = Header + Payload。
struct Packet {
    PacketHeader header;
    std::vector<std::uint8_t> payload;
};

} // namespace legend::network
