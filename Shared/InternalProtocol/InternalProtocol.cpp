#include "Shared/InternalProtocol/InternalProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace legend::internal {
namespace {

using legend::network::ByteReader;
using legend::network::ByteWriter;

bool Finish(ByteReader& reader, std::string& error) {
    if (!reader.IsValid() || reader.Remaining() != 0) {
        error = "malformed internal protocol payload";
        return false;
    }
    return true;
}

bool WriteBytes(ByteWriter& writer, const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > kMaxInternalPayloadBytes) {
        return false;
    }
    writer.WriteUInt32(static_cast<std::uint32_t>(bytes.size()));
    for (const auto byte : bytes) {
        writer.WriteUInt8(byte);
    }
    return true;
}

bool ReadBytes(ByteReader& reader, std::vector<std::uint8_t>& bytes) {
    const auto count = reader.ReadUInt32();
    if (!reader.IsValid() || count > kMaxInternalPayloadBytes || count > reader.Remaining()) {
        reader.Invalidate();
        return false;
    }
    bytes.clear();
    bytes.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        bytes.push_back(reader.ReadUInt8());
    }
    return reader.IsValid();
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

bool IsValidServiceType(ServiceType value) {
    return value >= ServiceType::LoginServer && value <= ServiceType::LogServer;
}

bool IsValidDbOperation(DbOperation value) {
    return value >= DbOperation::LoadAccount && value <= DbOperation::LoadCharacterFull;
}

bool IsValidLogEventType(LogEventType value) {
    return value >= LogEventType::LoginSuccess && value <= LogEventType::AdminAction;
}

const char* ServiceTypeName(ServiceType value) {
    switch (value) {
        case ServiceType::LoginServer: return "LoginServer";
        case ServiceType::CharacterServer: return "CharacterServer";
        case ServiceType::Gateway: return "Gateway";
        case ServiceType::WorldServer: return "WorldServer";
        case ServiceType::DbServer: return "DbServer";
        case ServiceType::LogServer: return "LogServer";
        default: return "Unknown";
    }
}

const char* LogEventTypeName(LogEventType value) {
    switch (value) {
        case LogEventType::LoginSuccess: return "LoginSuccess";
        case LogEventType::LoginFailure: return "LoginFailure";
        case LogEventType::CharacterCreate: return "CharacterCreate";
        case LogEventType::CharacterDelete: return "CharacterDelete";
        case LogEventType::CharacterSelect: return "CharacterSelect";
        case LogEventType::WorldEnter: return "WorldEnter";
        case LogEventType::WorldLeave: return "WorldLeave";
        case LogEventType::ShopBuy: return "ShopBuy";
        case LogEventType::ShopSell: return "ShopSell";
        case LogEventType::QuestComplete: return "QuestComplete";
        case LogEventType::ServerError: return "ServerError";
        case LogEventType::AdminAction: return "AdminAction";
        default: return "Unknown";
    }
}

bool EncodeServiceHandshake(const ServiceHandshake& value, std::vector<std::uint8_t>& out) {
    if (!IsValidServiceType(value.serviceType) ||
        value.protocolVersion != kInternalProtocolVersion || value.instanceId.empty()) {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.serviceType));
    writer.WriteUInt16(value.protocolVersion);
    return writer.WriteString(value.instanceId) && writer.WriteString(value.serviceToken);
}

bool DecodeServiceHandshake(const std::uint8_t* data, std::size_t size,
                            ServiceHandshake& out, std::string& error) {
    ByteReader reader(data, size);
    out.serviceType = static_cast<ServiceType>(reader.ReadUInt16());
    out.protocolVersion = reader.ReadUInt16();
    if (!reader.ReadString(out.instanceId) || !reader.ReadString(out.serviceToken) ||
        !Finish(reader, error)) {
        return false;
    }
    if (!IsValidServiceType(out.serviceType) ||
        out.protocolVersion != kInternalProtocolVersion || out.instanceId.empty()) {
        error = "invalid service handshake";
        return false;
    }
    return true;
}

bool EncodeServiceHandshakeAck(const ServiceHandshakeAck& value, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter writer(out);
    writer.WriteBool(value.accepted);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.errorCode));
    return writer.WriteString(value.message);
}

bool DecodeServiceHandshakeAck(const std::uint8_t* data, std::size_t size,
                               ServiceHandshakeAck& out, std::string& error) {
    ByteReader reader(data, size);
    out.accepted = reader.ReadBool();
    out.errorCode = static_cast<InternalErrorCode>(reader.ReadUInt16());
    return reader.ReadString(out.message) && Finish(reader, error);
}

bool EncodeHeartbeat(const Heartbeat& value, std::vector<std::uint8_t>& out) {
    if (value.health < ServiceHealth::Healthy || value.health > ServiceHealth::Unavailable) {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.WriteUInt64(value.sequence);
    writer.WriteUInt64(value.timestampMs);
    writer.WriteUInt8(static_cast<std::uint8_t>(value.health));
    return true;
}

bool DecodeHeartbeat(const std::uint8_t* data, std::size_t size,
                     Heartbeat& out, std::string& error) {
    ByteReader reader(data, size);
    out.sequence = reader.ReadUInt64();
    out.timestampMs = reader.ReadUInt64();
    out.health = static_cast<ServiceHealth>(reader.ReadUInt8());
    if (!Finish(reader, error) || out.health < ServiceHealth::Healthy ||
        out.health > ServiceHealth::Unavailable) {
        error = "invalid heartbeat";
        return false;
    }
    return true;
}

bool EncodeDbRequest(const DbRequest& value, std::vector<std::uint8_t>& out) {
    if (value.requestId == 0 || !IsValidDbOperation(value.operation)) {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.WriteUInt64(value.requestId);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.operation));
    writer.WriteUInt64(value.expectedVersion);
    return WriteBytes(writer, value.payload);
}

bool DecodeDbRequest(const std::uint8_t* data, std::size_t size,
                     DbRequest& out, std::string& error) {
    ByteReader reader(data, size);
    out.requestId = reader.ReadUInt64();
    out.operation = static_cast<DbOperation>(reader.ReadUInt16());
    out.expectedVersion = reader.ReadUInt64();
    if (!ReadBytes(reader, out.payload) || !Finish(reader, error) || out.requestId == 0 ||
        !IsValidDbOperation(out.operation)) {
        error = "invalid db request";
        return false;
    }
    return true;
}

bool EncodeDbResponse(const DbResponse& value, std::vector<std::uint8_t>& out) {
    if (value.requestId == 0) {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.WriteUInt64(value.requestId);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.errorCode));
    writer.WriteUInt64(value.recordVersion);
    if (!writer.WriteString(value.message)) {
        return false;
    }
    return WriteBytes(writer, value.payload);
}

bool DecodeDbResponse(const std::uint8_t* data, std::size_t size,
                      DbResponse& out, std::string& error) {
    ByteReader reader(data, size);
    out.requestId = reader.ReadUInt64();
    out.errorCode = static_cast<InternalErrorCode>(reader.ReadUInt16());
    out.recordVersion = reader.ReadUInt64();
    if (!reader.ReadString(out.message) || !ReadBytes(reader, out.payload) ||
        !Finish(reader, error) || out.requestId == 0) {
        error = "invalid db response";
        return false;
    }
    return true;
}

bool ContainsSensitiveLogField(const std::string& message, const std::string& extraJson) {
    const auto text = Lower(message + " " + extraJson);
    static constexpr std::array<const char*, 7> keys = {
        "password", "passwordhash", "password_hash", "sessiontoken",
        "session_token", "selectionticket", "service_token"};
    return std::any_of(keys.begin(), keys.end(),
                       [&text](const char* key) { return text.find(key) != std::string::npos; });
}

bool EncodeLogEvent(const LogEvent& value, std::vector<std::uint8_t>& out) {
    if (!IsValidServiceType(value.service) || !IsValidLogEventType(value.eventType) ||
        value.extraJson.size() > kMaxLogExtraBytes ||
        ContainsSensitiveLogField(value.message, value.extraJson)) {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.WriteUInt64(value.timestampMs);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.service));
    writer.WriteUInt16(value.level);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.eventType));
    writer.WriteUInt64(value.accountId);
    writer.WriteUInt64(value.characterId);
    return writer.WriteString(value.message) && writer.WriteString(value.extraJson);
}

bool DecodeLogEvent(const std::uint8_t* data, std::size_t size,
                    LogEvent& out, std::string& error) {
    ByteReader reader(data, size);
    out.timestampMs = reader.ReadUInt64();
    out.service = static_cast<ServiceType>(reader.ReadUInt16());
    out.level = reader.ReadUInt16();
    out.eventType = static_cast<LogEventType>(reader.ReadUInt16());
    out.accountId = reader.ReadUInt64();
    out.characterId = reader.ReadUInt64();
    if (!reader.ReadString(out.message) || !reader.ReadString(out.extraJson) ||
        !Finish(reader, error) || !IsValidServiceType(out.service) ||
        !IsValidLogEventType(out.eventType) || out.extraJson.size() > kMaxLogExtraBytes ||
        ContainsSensitiveLogField(out.message, out.extraJson)) {
        error = "invalid or sensitive log event";
        return false;
    }
    return true;
}

} // namespace legend::internal
