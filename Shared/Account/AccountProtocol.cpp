#include "Shared/Account/AccountProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::account {
namespace {

// 与 Shared/Network/Protocol.cpp 相同的模式：Encode 失败清空 out（不产生半包），
// Decode 必须完整消费（剩余字节 = malformed）。
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
        error = "malformed account payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed account payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

void WriteCharacterSummary(legend::network::ByteWriter& writer, const CharacterSummary& summary) {
    writer.WriteUInt64(summary.characterId);
    writer.WriteString(summary.name);
    writer.WriteUInt16(summary.classId);
    writer.WriteUInt16(summary.gender);
    writer.WriteUInt32(summary.level);
    writer.WriteUInt16(summary.mapId);
    writer.WriteUInt64(static_cast<std::uint64_t>(summary.lastPlayedAt));
    writer.WriteUInt16(summary.visualId); // 阶段26 指令十一：造型槽位
}

bool ReadCharacterSummary(legend::network::ByteReader& reader, CharacterSummary& out) {
    out.characterId = reader.ReadUInt64();
    if (!reader.ReadString(out.name)) {
        return false;
    }
    out.classId = reader.ReadUInt16();
    out.gender = reader.ReadUInt16();
    out.level = reader.ReadUInt32();
    out.mapId = reader.ReadUInt16();
    out.lastPlayedAt = static_cast<std::int64_t>(reader.ReadUInt64());
    out.visualId = reader.ReadUInt16();
    return reader.IsValid();
}

bool EncodeRegisterRequest(const RegisterRequestPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        return w.WriteString(p.username) && w.WriteString(p.password);
    });
}

bool DecodeRegisterRequest(const std::uint8_t* data, std::size_t size,
                           RegisterRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        (void)(r.ReadString(out.username) && r.ReadString(out.password));
    });
}

bool EncodeRegisterResponse(const RegisterResponsePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeRegisterResponse(const std::uint8_t* data, std::size_t size,
                            RegisterResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeAccountLoginRequest(const AccountLoginRequestPayload& p,
                               std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        return w.WriteString(p.username) && w.WriteString(p.password);
    });
}

bool DecodeAccountLoginRequest(const std::uint8_t* data, std::size_t size,
                               AccountLoginRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        (void)(r.ReadString(out.username) && r.ReadString(out.password));
    });
}

bool EncodeAccountLoginResponse(const AccountLoginResponsePayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt64(static_cast<std::uint64_t>(p.expiresAt));
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.sessionToken) && w.WriteString(p.message);
    });
}

bool DecodeAccountLoginResponse(const std::uint8_t* data, std::size_t size,
                                AccountLoginResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.expiresAt = static_cast<std::int64_t>(r.ReadUInt64());
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.sessionToken) && r.ReadString(out.message));
    });
}

bool EncodeSessionResumeRequest(const SessionResumeRequestPayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        return w.WriteString(p.sessionToken);
    });
}

bool DecodeSessionResumeRequest(const std::uint8_t* data, std::size_t size,
                                SessionResumeRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        (void)(r.ReadString(out.sessionToken));
    });
}

bool EncodeSessionResumeResponse(const SessionResumeResponsePayload& p,
                                 std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt64(static_cast<std::uint64_t>(p.expiresAt));
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeSessionResumeResponse(const std::uint8_t* data, std::size_t size,
                                 SessionResumeResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.expiresAt = static_cast<std::int64_t>(r.ReadUInt64());
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeCharacterListRequest(const CharacterListRequestPayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        return w.WriteString(p.sessionToken);
    });
}

bool DecodeCharacterListRequest(const std::uint8_t* data, std::size_t size,
                                CharacterListRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        (void)(r.ReadString(out.sessionToken));
    });
}

bool EncodeCharacterListResponse(const CharacterListResponsePayload& p,
                                 std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt16(static_cast<std::uint16_t>(p.characters.size()));
        for (const CharacterSummary& summary : p.characters) {
            WriteCharacterSummary(w, summary);
        }
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeCharacterListResponse(const std::uint8_t* data, std::size_t size,
                                 CharacterListResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        const std::uint16_t count = r.ReadUInt16();
        out.characters.reserve(count);
        for (std::uint16_t i = 0; i < count && r.IsValid(); ++i) {
            CharacterSummary summary;
            if (!ReadCharacterSummary(r, summary)) {
                return;
            }
            out.characters.push_back(std::move(summary));
        }
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeCharacterCreateRequest(const CharacterCreateRequestPayload& p,
                                  std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt16(p.classId);
        w.WriteUInt16(p.gender);
        w.WriteUInt16(p.visualId); // 阶段26 指令十一：初始造型
        return w.WriteString(p.sessionToken) && w.WriteString(p.name);
    });
}

bool DecodeCharacterCreateRequest(const std::uint8_t* data, std::size_t size,
                                  CharacterCreateRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.classId = r.ReadUInt16();
        out.gender = r.ReadUInt16();
        out.visualId = r.ReadUInt16();
        (void)(r.ReadString(out.sessionToken) && r.ReadString(out.name));
    });
}

bool EncodeCharacterCreateResponse(const CharacterCreateResponsePayload& p,
                                   std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        WriteCharacterSummary(w, p.character);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeCharacterCreateResponse(const std::uint8_t* data, std::size_t size,
                                   CharacterCreateResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        if (!ReadCharacterSummary(r, out.character)) {
            return;
        }
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeCharacterDeleteRequest(const CharacterDeleteRequestPayload& p,
                                  std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt64(p.characterId);
        return w.WriteString(p.sessionToken);
    });
}

bool DecodeCharacterDeleteRequest(const std::uint8_t* data, std::size_t size,
                                  CharacterDeleteRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.characterId = r.ReadUInt64();
        (void)(r.ReadString(out.sessionToken));
    });
}

bool EncodeCharacterDeleteResponse(const CharacterDeleteResponsePayload& p,
                                   std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.characterId);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeCharacterDeleteResponse(const std::uint8_t* data, std::size_t size,
                                   CharacterDeleteResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.characterId = r.ReadUInt64();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeCharacterSelectRequest(const CharacterSelectRequestPayload& p,
                                  std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt64(p.characterId);
        return w.WriteString(p.sessionToken);
    });
}

bool DecodeCharacterSelectRequest(const std::uint8_t* data, std::size_t size,
                                  CharacterSelectRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.characterId = r.ReadUInt64();
        (void)(r.ReadString(out.sessionToken));
    });
}

bool EncodeCharacterSelectResponse(const CharacterSelectResponsePayload& p,
                                   std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        WriteCharacterSummary(w, p.character);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.selectionTicket) && w.WriteString(p.message);
    });
}

bool DecodeCharacterSelectResponse(const std::uint8_t* data, std::size_t size,
                                   CharacterSelectResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        if (!ReadCharacterSummary(r, out.character)) {
            return;
        }
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.selectionTicket) && r.ReadString(out.message));
    });
}

bool EncodeAccountEnvelope(const AccountEnvelope& envelope, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(envelope.requestId);
        w.WriteUInt64(envelope.clientConnectionId);
        w.WriteUInt16(envelope.innerMessageId);
        w.WriteUInt32(static_cast<std::uint32_t>(envelope.innerPayload.size()));
        for (const std::uint8_t byte : envelope.innerPayload) {
            w.WriteUInt8(byte);
        }
        return true;
    });
}

bool DecodeAccountEnvelope(const std::uint8_t* data, std::size_t size,
                           AccountEnvelope& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.clientConnectionId = r.ReadUInt64();
        out.innerMessageId = r.ReadUInt16();
        const std::uint32_t innerSize = r.ReadUInt32();
        if (innerSize > r.Remaining()) {
            return; // 越界 -> IsValid()=false
        }
        out.innerPayload.resize(innerSize);
        for (std::uint32_t i = 0; i < innerSize; ++i) {
            out.innerPayload[i] = r.ReadUInt8();
        }
    });
}

} // namespace legend::account
