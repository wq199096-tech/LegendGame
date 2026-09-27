#include "Shared/Network/Protocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::network {
namespace {

// 通用模式：Encode 失败（超长 String）时清空 out，保证不产生半包。
template <typename Fn>
bool EncodePayload(std::vector<std::uint8_t>& out, Fn&& fill) {
    out.clear();
    ByteWriter writer(out);
    if (!fill(writer)) {
        out.clear();
        return false;
    }
    return true;
}

template <typename Fn>
bool DecodePayload(const std::uint8_t* data, std::size_t size, std::string& error, Fn&& fill) {
    ByteReader reader(data, size);
    fill(reader);
    if (!reader.IsValid()) {
        error = "malformed payload";
        return false;
    }
    return true;
}

} // namespace

bool EncodeClientHello(const ClientHelloPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteUInt16(p.protocolVersion);
        return w.WriteString(p.clientBuild) && w.WriteString(p.clientName);
    });
}

bool DecodeClientHello(const std::uint8_t* data, std::size_t size,
                       ClientHelloPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.protocolVersion = r.ReadUInt16();
        (void)(r.ReadString(out.clientBuild) && r.ReadString(out.clientName));
    });
}

bool EncodeServerHello(const ServerHelloPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteBool(p.accepted);
        w.WriteUInt16(p.protocolVersion);
        w.WriteUInt64(p.connectionId);
        return w.WriteString(p.serverName) && w.WriteString(p.message);
    });
}

bool DecodeServerHello(const std::uint8_t* data, std::size_t size,
                       ServerHelloPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.accepted = r.ReadBool();
        out.protocolVersion = r.ReadUInt16();
        out.connectionId = r.ReadUInt64();
        (void)(r.ReadString(out.serverName) && r.ReadString(out.message));
    });
}

bool EncodeLoginRequest(const LoginRequestPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        return w.WriteString(p.username) && w.WriteString(p.token);
    });
}

bool DecodeLoginRequest(const std::uint8_t* data, std::size_t size,
                        LoginRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        (void)(r.ReadString(out.username) && r.ReadString(out.token));
    });
}

bool EncodeLoginResponse(const LoginResponsePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.displayName) && w.WriteString(p.message);
    });
}

bool DecodeLoginResponse(const std::uint8_t* data, std::size_t size,
                         LoginResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.displayName) && r.ReadString(out.message));
    });
}

bool EncodeHeartbeatPing(const HeartbeatPingPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteUInt32(p.pingSequence);
        w.WriteUInt64(p.clientTimeMs);
        return true;
    });
}

bool DecodeHeartbeatPing(const std::uint8_t* data, std::size_t size,
                         HeartbeatPingPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.pingSequence = r.ReadUInt32();
        out.clientTimeMs = r.ReadUInt64();
    });
}

bool EncodeHeartbeatPong(const HeartbeatPongPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteUInt32(p.pingSequence);
        w.WriteUInt64(p.serverTimeMs);
        return true;
    });
}

bool DecodeHeartbeatPong(const std::uint8_t* data, std::size_t size,
                         HeartbeatPongPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.pingSequence = r.ReadUInt32();
        out.serverTimeMs = r.ReadUInt64();
    });
}

bool EncodeDisconnectNotice(const DisconnectNoticePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) { return w.WriteString(p.reason); });
}

bool DecodeDisconnectNotice(const std::uint8_t* data, std::size_t size,
                            DisconnectNoticePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) { (void)r.ReadString(out.reason); });
}

bool EncodeGatewayLoginForward(const GatewayLoginForwardPayload& p,
                               std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt64(p.clientConnectionId);
        return w.WriteString(p.username) && w.WriteString(p.token);
    });
}

bool DecodeGatewayLoginForward(const std::uint8_t* data, std::size_t size,
                               GatewayLoginForwardPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.clientConnectionId = r.ReadUInt64();
        (void)(r.ReadString(out.username) && r.ReadString(out.token));
    });
}

bool EncodeLoginGatewayResponse(const LoginGatewayResponsePayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt64(p.clientConnectionId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.displayName) && w.WriteString(p.message);
    });
}

bool DecodeLoginGatewayResponse(const std::uint8_t* data, std::size_t size,
                                LoginGatewayResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.clientConnectionId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.displayName) && r.ReadString(out.message));
    });
}

bool EncodeErrorResponse(const ErrorResponsePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](ByteWriter& w) {
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeErrorResponse(const std::uint8_t* data, std::size_t size,
                         ErrorResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](ByteReader& r) {
        out.errorCode = r.ReadUInt16();
        (void)r.ReadString(out.message);
    });
}

} // namespace legend::network
