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
    writer.WriteUInt16(value.visualId);
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
    value.visualId = reader.ReadUInt16();
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
    writer.WriteUInt16(value.classId); writer.WriteUInt16(value.gender);
    writer.WriteUInt16(value.visualId); return true;
}
bool DecodeCharacterCreateCommand(const std::uint8_t* data, std::size_t size,
                                  CharacterCreateCommand& out, std::string& error) {
    ByteReader reader(data, size); out.accountId = reader.ReadUInt64();
    if (!reader.ReadString(out.name)) return false;
    out.classId = reader.ReadUInt16(); out.gender = reader.ReadUInt16();
    out.visualId = reader.ReadUInt16(); return Finish(reader, error);
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

// ---------------------------------------------------------------------------
// 阶段25.5：World 持久化 RPC 编解码（严格校验；int64 走 UInt64 位宽约定）。
// ---------------------------------------------------------------------------
bool EncodeCharacterIdQuery(const CharacterIdQuery& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.characterId); return true;
}
bool DecodeCharacterIdQuery(const std::uint8_t* data, std::size_t size,
                            CharacterIdQuery& out, std::string& error) {
    ByteReader reader(data, size); out.characterId = reader.ReadUInt64();
    return Finish(reader, error);
}
bool EncodeWorldInventoryList(const WorldInventoryList& value, std::vector<std::uint8_t>& out) {
    if (value.items.size() > kMaxWorldInventoryItems) return false;
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.items.size()));
    for (const auto& item : value.items) {
        writer.WriteUInt64(item.instanceId);
        writer.WriteUInt32(item.definitionId);
        writer.WriteUInt32(item.quantity);
        writer.WriteUInt64(static_cast<std::uint64_t>(item.slotIndex));
        writer.WriteUInt64(static_cast<std::uint64_t>(item.createdAt));
    }
    return true;
}
bool DecodeWorldInventoryList(const std::uint8_t* data, std::size_t size,
                              WorldInventoryList& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    const auto count = reader.ReadUInt16();
    if (count > kMaxWorldInventoryItems) { error = "too many inventory items"; return false; }
    out.items.clear(); out.items.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        WorldInventoryItem item;
        item.instanceId = reader.ReadUInt64();
        item.definitionId = reader.ReadUInt32();
        item.quantity = reader.ReadUInt32();
        item.slotIndex = static_cast<std::int64_t>(reader.ReadUInt64());
        item.createdAt = static_cast<std::int64_t>(reader.ReadUInt64());
        if (!reader.IsValid()) return false;
        out.items.push_back(item);
    }
    return Finish(reader, error);
}
bool EncodeWorldItemInsert(const WorldItemInsert& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.definitionId);
    writer.WriteUInt32(value.quantity);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.slotIndex));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.createdAt));
    writer.WriteBool(value.mergedIntoStack);
    writer.WriteUInt64(value.mergeInstanceId);
    writer.WriteUInt32(value.mergeQuantity);
    return true;
}
bool DecodeWorldItemInsert(const std::uint8_t* data, std::size_t size,
                           WorldItemInsert& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.definitionId = reader.ReadUInt32();
    out.quantity = reader.ReadUInt32();
    out.slotIndex = static_cast<std::int64_t>(reader.ReadUInt64());
    out.createdAt = static_cast<std::int64_t>(reader.ReadUInt64());
    out.mergedIntoStack = reader.ReadBool();
    out.mergeInstanceId = reader.ReadUInt64();
    out.mergeQuantity = reader.ReadUInt32();
    return Finish(reader, error);
}
bool EncodeWorldItemInsertResult(const WorldItemInsertResult& value,
                                 std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.newInstanceId); return true;
}
bool DecodeWorldItemInsertResult(const std::uint8_t* data, std::size_t size,
                                 WorldItemInsertResult& out, std::string& error) {
    ByteReader reader(data, size); out.newInstanceId = reader.ReadUInt64();
    return Finish(reader, error);
}
bool EncodeWorldEquipItem(const WorldEquipItem& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt64(value.newItemInstanceId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.equipmentSlotCode));
    writer.WriteBool(value.hadPrevious);
    writer.WriteUInt64(value.previousInstanceId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.freedBagSlotIndex));
    return true;
}
bool DecodeWorldEquipItem(const std::uint8_t* data, std::size_t size,
                          WorldEquipItem& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.newItemInstanceId = reader.ReadUInt64();
    out.equipmentSlotCode = static_cast<std::int64_t>(reader.ReadUInt64());
    out.hadPrevious = reader.ReadBool();
    out.previousInstanceId = reader.ReadUInt64();
    out.freedBagSlotIndex = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldUnequipItem(const WorldUnequipItem& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.instanceId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.equipmentSlotCode));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.bagSlotIndex));
    return true;
}
bool DecodeWorldUnequipItem(const std::uint8_t* data, std::size_t size,
                            WorldUnequipItem& out, std::string& error) {
    ByteReader reader(data, size);
    out.instanceId = reader.ReadUInt64();
    out.equipmentSlotCode = static_cast<std::int64_t>(reader.ReadUInt64());
    out.bagSlotIndex = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldQuestInsert(const WorldQuestInsert& value, std::vector<std::uint8_t>& out) {
    if (value.objectiveIds.size() > kMaxWorldQuestObjectives) return false;
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.questId);
    writer.WriteUInt8(value.state);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.acceptedAt));
    writer.WriteUInt16(static_cast<std::uint16_t>(value.objectiveIds.size()));
    for (const auto objectiveId : value.objectiveIds) writer.WriteUInt32(objectiveId);
    return true;
}
bool DecodeWorldQuestInsert(const std::uint8_t* data, std::size_t size,
                            WorldQuestInsert& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.questId = reader.ReadUInt32();
    out.state = reader.ReadUInt8();
    out.acceptedAt = static_cast<std::int64_t>(reader.ReadUInt64());
    const auto count = reader.ReadUInt16();
    if (count > kMaxWorldQuestObjectives) { error = "too many quest objectives"; return false; }
    out.objectiveIds.clear(); out.objectiveIds.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        out.objectiveIds.push_back(reader.ReadUInt32());
    }
    return reader.IsValid() && Finish(reader, error);
}
bool EncodeWorldQuestTurnIn(const WorldQuestTurnIn& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.questId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.turnedInAt));
    writer.WriteUInt32(value.newLevel);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.newExperience));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.newGold));
    writer.WriteUInt32(value.rewardItemDefinitionId);
    writer.WriteUInt32(value.rewardItemQuantity);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.rewardItemSlotIndex));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.rewardItemCreatedAt));
    return true;
}
bool DecodeWorldQuestTurnIn(const std::uint8_t* data, std::size_t size,
                            WorldQuestTurnIn& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.questId = reader.ReadUInt32();
    out.turnedInAt = static_cast<std::int64_t>(reader.ReadUInt64());
    out.newLevel = reader.ReadUInt32();
    out.newExperience = static_cast<std::int64_t>(reader.ReadUInt64());
    out.newGold = static_cast<std::int64_t>(reader.ReadUInt64());
    out.rewardItemDefinitionId = reader.ReadUInt32();
    out.rewardItemQuantity = reader.ReadUInt32();
    out.rewardItemSlotIndex = static_cast<std::int64_t>(reader.ReadUInt64());
    out.rewardItemCreatedAt = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldQuestTurnInResult(const WorldQuestTurnInResult& value,
                                  std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteBool(value.ok);
    writer.WriteUInt64(value.itemInstanceId);
    return true;
}
bool DecodeWorldQuestTurnInResult(const std::uint8_t* data, std::size_t size,
                                  WorldQuestTurnInResult& out, std::string& error) {
    ByteReader reader(data, size);
    out.ok = reader.ReadBool();
    out.itemInstanceId = reader.ReadUInt64();
    return Finish(reader, error);
}
bool EncodeWorldQuestAbandon(const WorldQuestAbandon& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.questId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.nowUnix));
    return true;
}
bool DecodeWorldQuestAbandon(const std::uint8_t* data, std::size_t size,
                             WorldQuestAbandon& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.questId = reader.ReadUInt32();
    out.nowUnix = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldQuestObjective(const WorldQuestObjective& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.questId);
    writer.WriteUInt32(value.objectiveId);
    writer.WriteUInt32(value.progress);
    return true;
}
bool DecodeWorldQuestObjective(const std::uint8_t* data, std::size_t size,
                               WorldQuestObjective& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.questId = reader.ReadUInt32();
    out.objectiveId = reader.ReadUInt32();
    out.progress = reader.ReadUInt32();
    return Finish(reader, error);
}
bool EncodeWorldQuestState(const WorldQuestState& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.questId);
    writer.WriteUInt8(value.state);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.timestamp));
    writer.WriteBool(value.setTurnedInAt);
    return true;
}
bool DecodeWorldQuestState(const std::uint8_t* data, std::size_t size,
                           WorldQuestState& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.questId = reader.ReadUInt32();
    out.state = reader.ReadUInt8();
    out.timestamp = static_cast<std::int64_t>(reader.ReadUInt64());
    out.setTurnedInAt = reader.ReadBool();
    return Finish(reader, error);
}
bool EncodeWorldOfflineKill(const WorldOfflineKill& value, std::vector<std::uint8_t>& out) {
    if (value.candidates.size() > kMaxWorldKillCandidates) return false;
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.candidates.size()));
    for (const auto& candidate : value.candidates) {
        writer.WriteUInt32(candidate.questId);
        writer.WriteUInt32(candidate.objectiveId);
        writer.WriteUInt32(candidate.requiredCount);
    }
    return true;
}
bool DecodeWorldOfflineKill(const std::uint8_t* data, std::size_t size,
                            WorldOfflineKill& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    const auto count = reader.ReadUInt16();
    if (count > kMaxWorldKillCandidates) { error = "too many kill candidates"; return false; }
    out.candidates.clear(); out.candidates.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        WorldQuestKillCandidate candidate;
        candidate.questId = reader.ReadUInt32();
        candidate.objectiveId = reader.ReadUInt32();
        candidate.requiredCount = reader.ReadUInt32();
        if (!reader.IsValid()) return false;
        out.candidates.push_back(candidate);
    }
    return Finish(reader, error);
}
bool EncodeWorldSavePosition(const WorldSavePosition& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt16(value.mapId);
    writer.WriteFloat(value.positionX);
    writer.WriteFloat(value.positionY);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.lastPlayedAt));
    return true;
}
bool DecodeWorldSavePosition(const std::uint8_t* data, std::size_t size,
                             WorldSavePosition& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.mapId = reader.ReadUInt16();
    out.positionX = reader.ReadFloat();
    out.positionY = reader.ReadFloat();
    out.lastPlayedAt = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldSaveProgression(const WorldSaveProgression& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt32(value.level);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.exp));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.gold));
    return true;
}
bool DecodeWorldSaveProgression(const std::uint8_t* data, std::size_t size,
                                WorldSaveProgression& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.level = reader.ReadUInt32();
    out.exp = static_cast<std::int64_t>(reader.ReadUInt64());
    out.gold = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldAddRewards(const WorldAddRewards& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.expDelta));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.goldDelta));
    return true;
}
bool DecodeWorldAddRewards(const std::uint8_t* data, std::size_t size,
                           WorldAddRewards& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.expDelta = static_cast<std::int64_t>(reader.ReadUInt64());
    out.goldDelta = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldSaveGold(const WorldSaveGold& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.gold));
    return true;
}
bool DecodeWorldSaveGold(const std::uint8_t* data, std::size_t size,
                         WorldSaveGold& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.gold = static_cast<std::int64_t>(reader.ReadUInt64());
    return Finish(reader, error);
}
bool EncodeWorldShopBuy(const WorldShopBuy& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.newGold));
    writer.WriteUInt32(value.itemDefinitionId);
    writer.WriteUInt32(value.quantity);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.bagSlotIndex));
    writer.WriteBool(value.mergedIntoStack);
    writer.WriteUInt64(value.mergeInstanceId);
    writer.WriteUInt32(value.mergeQuantity);
    return true;
}
bool DecodeWorldShopBuy(const std::uint8_t* data, std::size_t size,
                        WorldShopBuy& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.newGold = static_cast<std::int64_t>(reader.ReadUInt64());
    out.itemDefinitionId = reader.ReadUInt32();
    out.quantity = reader.ReadUInt32();
    out.bagSlotIndex = static_cast<std::int64_t>(reader.ReadUInt64());
    out.mergedIntoStack = reader.ReadBool();
    out.mergeInstanceId = reader.ReadUInt64();
    out.mergeQuantity = reader.ReadUInt32();
    return Finish(reader, error);
}
bool EncodeWorldShopBuyResult(const WorldShopBuyResult& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out); writer.WriteUInt64(value.newInstanceId); return true;
}
bool DecodeWorldShopBuyResult(const std::uint8_t* data, std::size_t size,
                              WorldShopBuyResult& out, std::string& error) {
    ByteReader reader(data, size); out.newInstanceId = reader.ReadUInt64();
    return Finish(reader, error);
}
bool EncodeWorldShopSell(const WorldShopSell& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.characterId);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.newGold));
    writer.WriteUInt64(value.instanceId);
    writer.WriteUInt32(value.quantity);
    writer.WriteBool(value.wholeStack);
    return true;
}
bool DecodeWorldShopSell(const std::uint8_t* data, std::size_t size,
                         WorldShopSell& out, std::string& error) {
    ByteReader reader(data, size);
    out.characterId = reader.ReadUInt64();
    out.newGold = static_cast<std::int64_t>(reader.ReadUInt64());
    out.instanceId = reader.ReadUInt64();
    out.quantity = reader.ReadUInt32();
    out.wholeStack = reader.ReadBool();
    return Finish(reader, error);
}
bool EncodeWorldCharacterRow(const WorldCharacterRow& value, std::vector<std::uint8_t>& out) {
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt64(value.id);
    writer.WriteUInt64(value.accountId);
    if (!writer.WriteString(value.name)) return false;
    writer.WriteUInt16(value.classId);
    writer.WriteUInt16(value.gender);
    writer.WriteUInt32(value.level);
    writer.WriteUInt64(static_cast<std::uint64_t>(value.exp));
    writer.WriteUInt64(static_cast<std::uint64_t>(value.gold));
    writer.WriteUInt16(value.mapId);
    writer.WriteFloat(value.positionX);
    writer.WriteFloat(value.positionY);
    writer.WriteBool(value.deleted);
    writer.WriteUInt16(value.visualId); // Stage27 指令五：持久化造型
    return true;
}
bool DecodeWorldCharacterRow(const std::uint8_t* data, std::size_t size,
                             WorldCharacterRow& out, std::string& error) {
    ByteReader reader(data, size);
    out.id = reader.ReadUInt64();
    out.accountId = reader.ReadUInt64();
    if (!reader.ReadString(out.name)) return false;
    out.classId = reader.ReadUInt16();
    out.gender = reader.ReadUInt16();
    out.level = reader.ReadUInt32();
    out.exp = static_cast<std::int64_t>(reader.ReadUInt64());
    out.gold = static_cast<std::int64_t>(reader.ReadUInt64());
    out.mapId = reader.ReadUInt16();
    out.positionX = reader.ReadFloat();
    out.positionY = reader.ReadFloat();
    out.deleted = reader.ReadBool();
    out.visualId = reader.ReadUInt16();
    return Finish(reader, error);
}
bool EncodeWorldQuestStateList(const WorldQuestStateList& value, std::vector<std::uint8_t>& out) {
    if (value.quests.size() > kMaxWorldQuestRows ||
        value.objectives.size() > kMaxWorldQuestRows * kMaxWorldQuestObjectives) {
        return false;
    }
    out.clear(); ByteWriter writer(out);
    writer.WriteUInt16(static_cast<std::uint16_t>(value.quests.size()));
    for (const auto& quest : value.quests) {
        writer.WriteUInt32(quest.questId);
        writer.WriteUInt8(quest.state);
        writer.WriteUInt64(static_cast<std::uint64_t>(quest.acceptedAt));
        writer.WriteUInt64(static_cast<std::uint64_t>(quest.completedAt));
        writer.WriteUInt64(static_cast<std::uint64_t>(quest.turnedInAt));
    }
    writer.WriteUInt16(static_cast<std::uint16_t>(value.objectives.size()));
    for (const auto& objective : value.objectives) {
        writer.WriteUInt32(objective.questId);
        writer.WriteUInt32(objective.objectiveId);
        writer.WriteUInt32(objective.progress);
    }
    return true;
}
bool DecodeWorldQuestStateList(const std::uint8_t* data, std::size_t size,
                               WorldQuestStateList& out, std::string& error) {
    ByteReader reader(data, size);
    const auto questCount = reader.ReadUInt16();
    if (questCount > kMaxWorldQuestRows) { error = "too many quest rows"; return false; }
    out.quests.clear(); out.quests.reserve(questCount);
    for (std::uint32_t i = 0; i < questCount; ++i) {
        WorldQuestRow quest;
        quest.questId = reader.ReadUInt32();
        quest.state = reader.ReadUInt8();
        quest.acceptedAt = static_cast<std::int64_t>(reader.ReadUInt64());
        quest.completedAt = static_cast<std::int64_t>(reader.ReadUInt64());
        quest.turnedInAt = static_cast<std::int64_t>(reader.ReadUInt64());
        if (!reader.IsValid()) return false;
        out.quests.push_back(quest);
    }
    const auto objectiveCount = reader.ReadUInt16();
    if (objectiveCount > kMaxWorldQuestRows * kMaxWorldQuestObjectives) {
        error = "too many objective rows"; return false;
    }
    out.objectives.clear(); out.objectives.reserve(objectiveCount);
    for (std::uint32_t i = 0; i < objectiveCount; ++i) {
        WorldObjectiveRow objective;
        objective.questId = reader.ReadUInt32();
        objective.objectiveId = reader.ReadUInt32();
        objective.progress = reader.ReadUInt32();
        if (!reader.IsValid()) return false;
        out.objectives.push_back(objective);
    }
    return Finish(reader, error);
}
} // namespace legend::internal
