#include "Shared/Network/PacketCodec.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::network {

void PacketCodec::EncodeHeader(const PacketHeader& header, std::vector<std::uint8_t>& out) {
    ByteWriter writer(out);
    writer.WriteUInt32(header.magic);
    writer.WriteUInt16(header.version);
    writer.WriteUInt16(header.messageId);
    writer.WriteUInt32(header.payloadSize);
    writer.WriteUInt32(header.sequence);
}

bool PacketCodec::DecodeHeader(const std::uint8_t* data, std::size_t size,
                               PacketHeader& out, std::string& error) {
    if (size < kPacketHeaderSize) {
        error = "header truncated";
        return false;
    }
    ByteReader reader(data, kPacketHeaderSize);
    out.magic = reader.ReadUInt32();
    out.version = reader.ReadUInt16();
    out.messageId = reader.ReadUInt16();
    out.payloadSize = reader.ReadUInt32();
    out.sequence = reader.ReadUInt32();
    // 指令九：magic 错误立即断开
    if (out.magic != kPacketMagic) {
        error = "bad magic";
        return false;
    }
    // 指令十一：payload 上限（防恶意/损坏 Header 触发超大内存分配）
    if (out.payloadSize > kMaxPacketPayload) {
        error = "payload too large";
        return false;
    }
    return true;
}

void PacketCodec::EncodePacket(const Packet& packet, std::vector<std::uint8_t>& out) {
    PacketHeader header = packet.header;
    header.payloadSize = static_cast<std::uint32_t>(packet.payload.size());
    EncodeHeader(header, out);
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
}

bool PacketCodec::DecodePacket(const std::uint8_t* data, std::size_t size,
                               Packet& out, std::string& error) {
    if (!DecodeHeader(data, size, out.header, error)) {
        return false;
    }
    const std::size_t payloadSize = out.header.payloadSize;
    if (size < kPacketHeaderSize + payloadSize) {
        error = "payload truncated";
        return false;
    }
    out.payload.assign(data + kPacketHeaderSize, data + kPacketHeaderSize + payloadSize);
    return true;
}

} // namespace legend::network
