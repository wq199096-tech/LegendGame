#include "Shared/Chat/ChatProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::chat {
namespace {

// 与 Shared/World/WorldProtocol.cpp 相同的模式（指令六十三）：Encode 失败清空 out
//（不产生半包），Decode 必须完整消费（剩余字节 = malformed）。
template <typename Fn>
bool EncodePayload(std::vector<std::uint8_t>& out, Fn&& fill) {
    out.clear();
    legend::network::ByteWriter writer(out);
    if (!fill(writer)) {
        out.clear();
        return false;
    }
    return true;
}

template <typename Fn>
bool DecodePayload(const std::uint8_t* data, std::size_t size, std::string& error, Fn&& fill) {
    legend::network::ByteReader reader(data, size);
    fill(reader);
    if (!reader.IsValid()) {
        error = "malformed chat payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed chat payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

bool EncodeChatSendRequest(const ChatSendRequestPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt8(p.channel);
        return w.WriteString(p.targetName) && w.WriteString(p.text);
    });
}

bool DecodeChatSendRequest(const std::uint8_t* data, std::size_t size,
                           ChatSendRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.channel = r.ReadUInt8();
        (void)(r.ReadString(out.targetName) && r.ReadString(out.text));
    });
}

bool EncodeChatSendResponse(const ChatSendResponsePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeChatSendResponse(const std::uint8_t* data, std::size_t size,
                            ChatSendResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeChatMessageEvent(const ChatMessageEventPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.messageId);
        w.WriteUInt8(p.channel);
        w.WriteUInt64(p.senderCharacterId);
        const bool stringsOk = w.WriteString(p.senderName) && w.WriteString(p.targetName) &&
                               w.WriteString(p.text);
        w.WriteUInt64(p.timestamp);
        return stringsOk;
    });
}

bool DecodeChatMessageEvent(const std::uint8_t* data, std::size_t size,
                            ChatMessageEventPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.messageId = r.ReadUInt64();
        out.channel = r.ReadUInt8();
        out.senderCharacterId = r.ReadUInt64();
        (void)(r.ReadString(out.senderName) && r.ReadString(out.targetName) &&
               r.ReadString(out.text));
        out.timestamp = r.ReadUInt64();
    });
}

} // namespace legend::chat
