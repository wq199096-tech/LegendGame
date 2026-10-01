#include "Shared/InternalProtocol/PersistenceMessages.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::internal {
namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;

bool Finish(ByteReader& reader, std::string& error) {
    if (!reader.IsValid() || reader.Remaining() != 0) {
        error = "malformed persistence payload";
        return false;
    }
    return true;
}

bool WriteSummary(ByteWriter& writer, const legend::account::CharacterSummary& value) {
    writer.WriteUInt64(value.characterId);
    if (!writer.WriteString(value.name)) return false;
    writer.WriteUInt16(value.classId);
    writer.WriteUInt16(value.gender);
    writer.WriteUInt32(value.level);
    writer.WriteUInt16(value.mapId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.lastPlayedAt));
    return true;
}

bool ReadSummary(ByteReader& reader, legend::account::CharacterSummary& value) {
    value.characterId = reader.ReadUInt64();
    if (!reader.ReadString(value.name)) return false;
    value.classId = reader.ReadUInt16();
    value.gender = reader.ReadUInt16();
    value.level = reader.ReadUInt32();
    value.mapId = reader.ReadUInt16();
    value.lastPlayedAt = static_cast<std::int64_t>(reader.ReadUInt64());
    return reader.IsValid();
}
} // namespace

bool EncodeAccountCredentials(const AccountCredentials& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    return writer.WriteString(value.username) && writer.WriteString(value.password);
}
bool DecodeAccountCredentials(const std::uint8_t* data, std::size_t size,
                              AccountCredentials& out, std::string& error) {
    ByteReader reader(data, size);
    return reader.ReadString(out.username) && reader.ReadString(out.password) && Finish(reader, error);
}
bool EncodeAccountRegisterResult(const AccountRegisterResult& value,
                                 std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.accountId); return true;
}
bool DecodeAccountRegisterResult(const std::uint8_t* data, std::size_t size,
                                 AccountRegisterResult& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64(); return Finish(reader, error);
}
bool EncodeAccountLoginResult(const AccountLoginResult& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.accountId);
    if (!writer.WriteString(value.sessionToken)) return false;
    writer.WriteUInt64(static_cast<std::uint64_t>(value.expiresAt)); return true;
}
bool DecodeAccountLoginResult(const std::uint8_t* data, std::size_t size,
                              AccountLoginResult& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64();
    if (!reader.ReadString(out.sessionToken)) return false;
    out.expiresAt = static_cast<std::int64_t>(reader.ReadUInt64()); return Finish(reader, error);
}

bool EncodeValidateSessionRequest(const ValidateSessionRequest& value,
                                  std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); return writer.WriteString(value.sessionToken);
}
bool DecodeValidateSessionRequest(const std::uint8_t* data, std::size_t size,
                                  ValidateSessionRequest& out, std::string& error) {
    ByteReader reader(data, size); return reader.ReadString(out.sessionToken) && Finish(reader, error);
}
bool EncodeValidateSessionResult(const ValidateSessionResult& value,
                                 std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.accountId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.expiresAt)); return true;
}
bool DecodeValidateSessionResult(const std::uint8_t* data, std::size_t size,
                                 ValidateSessionResult& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64();
    out.expiresAt = static_cast<std::int64_t>(reader.ReadUInt64()); return Finish(reader, error);
}
bool EncodeCharacterListQuery(const CharacterListQuery& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.accountId); return true;
}
bool DecodeCharacterListQuery(const std::uint8_t* data, std::size_t size,
                              CharacterListQuery& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64(); return Finish(reader, error);
}
bool EncodeCharacterCreateCommand(const CharacterCreateCommand& value,
                                  std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.accountId);
    if (!writer.WriteString(value.name)) return false;
    writer.WriteUInt16(value.classId); writer.WriteUInt16(value.gender); return true;
}
bool DecodeCharacterCreateCommand(const std::uint8_t* data, std::size_t size,
                                  CharacterCreateCommand& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64();
    if (!reader.ReadString(out.name)) return false;
    out.classId = reader.ReadUInt16(); out.gender = reader.ReadUInt16(); return Finish(reader, error);
}
bool EncodeCharacterCommand(const CharacterCommand& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.accountId);
    writer.WriteUInt64(value.characterId); return true;
}
bool DecodeCharacterCommand(const std::uint8_t* data, std::size_t size,
                            CharacterCommand& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64();
    out.characterId = reader.ReadUInt64(); return Finish(reader, error);
}
bool EncodeCharacterSummary(const legend::account::CharacterSummary& value,
                            std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); return WriteSummary(writer, value);
}
bool DecodeCharacterSummary(const std::uint8_t* data, std::size_t size,
                            legend::account::CharacterSummary& out, std::string& error) {
    ByteReader reader(data, size); return ReadSummary(reader, out) && Finish(reader, error);
}
bool EncodeCharacterList(const std::vector<legend::account::CharacterSummary>& value,
                         std::vector<std::uint8_t>& out) {
    if (value.size() > legend::account::kMaxCharactersPerAccount) return false;
    out.clear(); ByteWriter writer(out); writer.WriteUInt16(static_cast<std::uint16_t>(value.size()));
    for (const auto& summary : value) if (!WriteSummary(writer, summary)) return false;
    return true;
}
bool DecodeCharacterList(const std::uint8_t* data, std::size_t size,
                         std::vector<legend::account::CharacterSummary>& out,
                         std::string& error) {
    ByteReader reader(data, size); const auto count = reader.ReadUInt16();
    if (count > legend::account::kMaxCharactersPerAccount) { error = "too many characters"; return false; }
    out.clear(); out.resize(count);
    for (auto& summary : out) if (!ReadSummary(reader, summary)) return false;
    return Finish(reader, error);
}
} // namespace legend::internal
