#pragma once

#include "Shared/Account/CharacterTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::internal {

struct AccountCredentials { std::string username; std::string password; };
struct AccountRegisterResult { std::uint64_t accountId = 0; };
struct AccountLoginResult {
    std::uint64_t accountId = 0;
    std::string sessionToken;
    std::int64_t expiresAt = 0;
};
struct ValidateSessionRequest { std::string sessionToken; };
struct ValidateSessionResult { std::uint64_t accountId = 0; std::int64_t expiresAt = 0; };
struct CharacterListQuery { std::uint64_t accountId = 0; };
struct CharacterCreateCommand {
    std::uint64_t accountId = 0;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint16_t visualId = 1; // 阶段26 指令十一：初始造型槽位
};
struct CharacterCommand { std::uint64_t accountId = 0; std::uint64_t characterId = 0; };

bool EncodeAccountCredentials(const AccountCredentials& value, std::vector<std::uint8_t>& out);
bool DecodeAccountCredentials(const std::uint8_t* data, std::size_t size,
                              AccountCredentials& out, std::string& error);
bool EncodeAccountRegisterResult(const AccountRegisterResult& value,
                                 std::vector<std::uint8_t>& out);
bool DecodeAccountRegisterResult(const std::uint8_t* data, std::size_t size,
                                 AccountRegisterResult& out, std::string& error);
bool EncodeAccountLoginResult(const AccountLoginResult& value, std::vector<std::uint8_t>& out);
bool DecodeAccountLoginResult(const std::uint8_t* data, std::size_t size,
                              AccountLoginResult& out, std::string& error);

bool EncodeValidateSessionRequest(const ValidateSessionRequest& value,
                                  std::vector<std::uint8_t>& out);
bool DecodeValidateSessionRequest(const std::uint8_t* data, std::size_t size,
                                  ValidateSessionRequest& out, std::string& error);
bool EncodeValidateSessionResult(const ValidateSessionResult& value,
                                 std::vector<std::uint8_t>& out);
bool DecodeValidateSessionResult(const std::uint8_t* data, std::size_t size,
                                 ValidateSessionResult& out, std::string& error);
bool EncodeCharacterListQuery(const CharacterListQuery& value, std::vector<std::uint8_t>& out);
bool DecodeCharacterListQuery(const std::uint8_t* data, std::size_t size,
                              CharacterListQuery& out, std::string& error);
bool EncodeCharacterCreateCommand(const CharacterCreateCommand& value,
                                  std::vector<std::uint8_t>& out);
bool DecodeCharacterCreateCommand(const std::uint8_t* data, std::size_t size,
                                  CharacterCreateCommand& out, std::string& error);
bool EncodeCharacterCommand(const CharacterCommand& value, std::vector<std::uint8_t>& out);
bool DecodeCharacterCommand(const std::uint8_t* data, std::size_t size,
                            CharacterCommand& out, std::string& error);
bool EncodeCharacterSummary(const legend::account::CharacterSummary& value,
                            std::vector<std::uint8_t>& out);
bool DecodeCharacterSummary(const std::uint8_t* data, std::size_t size,
                            legend::account::CharacterSummary& out, std::string& error);
bool EncodeCharacterList(const std::vector<legend::account::CharacterSummary>& value,
                         std::vector<std::uint8_t>& out);
bool DecodeCharacterList(const std::uint8_t* data, std::size_t size,
                         std::vector<legend::account::CharacterSummary>& out,
                         std::string& error);

// ---------------------------------------------------------------------------
// 阶段25.5：World 持久化 RPC 命令/结果。
// 字段与 WorldServer 现用 Repository POD 一一对应（事务逻辑在 DbServer 内执行，
// World io 线程零 SQLite）；所有 Decode 严格校验（IsValid + Remaining()==0 +
// 数量上限拒绝）。
// ---------------------------------------------------------------------------
inline constexpr std::size_t kMaxWorldInventoryItems = 1024;
inline constexpr std::size_t kMaxWorldQuestObjectives = 64;
inline constexpr std::size_t kMaxWorldQuestRows = 128;
inline constexpr std::size_t kMaxWorldKillCandidates = 256;

struct CharacterIdQuery { std::uint64_t characterId = 0; };

// LoadInventory 结果行（对应 InventoryRepository::InventoryRow）。
struct WorldInventoryItem {
    std::uint64_t instanceId = 0;
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 0;
    std::int64_t slotIndex = 0;
    std::int64_t createdAt = 0;
};
struct WorldInventoryList {
    std::uint64_t characterId = 0;
    std::vector<WorldInventoryItem> items;
};

// ItemInsertWrite：拾取/入包（Insert 新堆叠 或 Merge 既有堆叠数量覆盖）。
struct WorldItemInsert {
    std::uint64_t characterId = 0;
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 0;
    std::int64_t slotIndex = 0;
    std::int64_t createdAt = 0;
    bool mergedIntoStack = false;
    std::uint64_t mergeInstanceId = 0;
    std::uint32_t mergeQuantity = 0;
};
struct WorldItemInsertResult { std::uint64_t newInstanceId = 0; };

// EquipItemWrite / UnequipItemWrite（对应 InventoryRepository 装备事务）。
struct WorldEquipItem {
    std::uint64_t characterId = 0;
    std::uint64_t newItemInstanceId = 0;
    std::int64_t equipmentSlotCode = 0; // 1001/1002
    bool hadPrevious = false;
    std::uint64_t previousInstanceId = 0;
    std::int64_t freedBagSlotIndex = 0;
};
struct WorldUnequipItem {
    std::uint64_t instanceId = 0;
    std::int64_t equipmentSlotCode = 0;
    std::int64_t bagSlotIndex = 0;
};

// QuestInsertWrite（对应 QuestRepository::InsertQuest）。
struct WorldQuestInsert {
    std::uint64_t characterId = 0;
    std::uint32_t questId = 0;
    std::uint8_t state = 0;
    std::int64_t acceptedAt = 0;
    std::vector<std::uint32_t> objectiveIds;
};
// QuestTurnInWrite（对应 QuestRepository::TurnInTransaction）。
struct WorldQuestTurnIn {
    std::uint64_t characterId = 0;
    std::uint32_t questId = 0;
    std::int64_t turnedInAt = 0;
    std::uint32_t newLevel = 1;
    std::int64_t newExperience = 0;
    std::int64_t newGold = 0;
    std::uint32_t rewardItemDefinitionId = 0;
    std::uint32_t rewardItemQuantity = 0;
    std::int64_t rewardItemSlotIndex = -1;
    std::int64_t rewardItemCreatedAt = 0;
};
struct WorldQuestTurnInResult {
    bool ok = false;
    std::uint64_t itemInstanceId = 0;
};
struct WorldQuestAbandon {
    std::uint64_t characterId = 0;
    std::uint32_t questId = 0;
    std::int64_t nowUnix = 0;
};
// QuestObjectiveWrite / QuestStateWrite。
struct WorldQuestObjective {
    std::uint64_t characterId = 0;
    std::uint32_t questId = 0;
    std::uint32_t objectiveId = 0;
    std::uint32_t progress = 0;
};
struct WorldQuestState {
    std::uint64_t characterId = 0;
    std::uint32_t questId = 0;
    std::uint8_t state = 0;
    std::int64_t timestamp = 0;
    bool setTurnedInAt = false;
};
// OfflineKillWrite（对应 QuestRepository::OfflineAdvanceKill）。
struct WorldQuestKillCandidate {
    std::uint32_t questId = 0;
    std::uint32_t objectiveId = 0;
    std::uint32_t requiredCount = 0;
};
struct WorldOfflineKill {
    std::uint64_t characterId = 0;
    std::vector<WorldQuestKillCandidate> candidates;
};

// SavePosition（对应 CharacterRepository::UpdateWorldPosition）。
struct WorldSavePosition {
    std::uint64_t characterId = 0;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::int64_t lastPlayedAt = 0;
};
// SaveProgression / AddRewardsWrite / SaveGoldWrite（成长写回）。
struct WorldSaveProgression {
    std::uint64_t characterId = 0;
    std::uint32_t level = 1;
    std::int64_t exp = 0;
    std::int64_t gold = 0;
};
struct WorldAddRewards {
    std::uint64_t characterId = 0;
    std::int64_t expDelta = 0;
    std::int64_t goldDelta = 0;
};
struct WorldSaveGold {
    std::uint64_t characterId = 0;
    std::int64_t gold = 0;
};
// ShopBuyWrite / ShopSellWrite（商店原子事务，SQL 从 WorldServer 抽入 Repository）。
struct WorldShopBuy {
    std::uint64_t characterId = 0;
    std::int64_t newGold = 0;
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 0;
    std::int64_t bagSlotIndex = 0;
    bool mergedIntoStack = false;
    std::uint64_t mergeInstanceId = 0;
    std::uint32_t mergeQuantity = 0;
};
struct WorldShopBuyResult { std::uint64_t newInstanceId = 0; };
struct WorldShopSell {
    std::uint64_t characterId = 0;
    std::int64_t newGold = 0;
    std::uint64_t instanceId = 0;
    std::uint32_t quantity = 0;
    bool wholeStack = false;
};

// LoadCharacterFull 结果（对应 CharacterRepository::CharacterRow 的 World 关心字段）。
struct WorldCharacterRow {
    std::uint64_t id = 0;
    std::uint64_t accountId = 0;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::int64_t exp = 0;
    std::int64_t gold = 0;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    bool deleted = false;
    // Stage27 指令五：持久化造型（Migration 6 characters.visual_id）——WorldServer
    // AOI PlayerSpawn 必须下发真实 visualId，禁止客户端按 classId 猜测。
    std::uint16_t visualId = 1;
};

// LoadQuestState 结果（对应 QuestRepository::QuestRow/ObjectiveRow）。
struct WorldQuestRow {
    std::uint32_t questId = 0;
    std::uint8_t state = 0;
    std::int64_t acceptedAt = 0;
    std::int64_t completedAt = 0;
    std::int64_t turnedInAt = 0;
};
struct WorldObjectiveRow {
    std::uint32_t questId = 0;
    std::uint32_t objectiveId = 0;
    std::uint32_t progress = 0;
};
struct WorldQuestStateList {
    std::vector<WorldQuestRow> quests;
    std::vector<WorldObjectiveRow> objectives;
};

bool EncodeCharacterIdQuery(const CharacterIdQuery& value, std::vector<std::uint8_t>& out);
bool DecodeCharacterIdQuery(const std::uint8_t* data, std::size_t size,
                            CharacterIdQuery& out, std::string& error);
bool EncodeWorldInventoryList(const WorldInventoryList& value, std::vector<std::uint8_t>& out);
bool DecodeWorldInventoryList(const std::uint8_t* data, std::size_t size,
                              WorldInventoryList& out, std::string& error);
bool EncodeWorldItemInsert(const WorldItemInsert& value, std::vector<std::uint8_t>& out);
bool DecodeWorldItemInsert(const std::uint8_t* data, std::size_t size,
                           WorldItemInsert& out, std::string& error);
bool EncodeWorldItemInsertResult(const WorldItemInsertResult& value,
                                 std::vector<std::uint8_t>& out);
bool DecodeWorldItemInsertResult(const std::uint8_t* data, std::size_t size,
                                 WorldItemInsertResult& out, std::string& error);
bool EncodeWorldEquipItem(const WorldEquipItem& value, std::vector<std::uint8_t>& out);
bool DecodeWorldEquipItem(const std::uint8_t* data, std::size_t size,
                          WorldEquipItem& out, std::string& error);
bool EncodeWorldUnequipItem(const WorldUnequipItem& value, std::vector<std::uint8_t>& out);
bool DecodeWorldUnequipItem(const std::uint8_t* data, std::size_t size,
                            WorldUnequipItem& out, std::string& error);
bool EncodeWorldQuestInsert(const WorldQuestInsert& value, std::vector<std::uint8_t>& out);
bool DecodeWorldQuestInsert(const std::uint8_t* data, std::size_t size,
                            WorldQuestInsert& out, std::string& error);
bool EncodeWorldQuestTurnIn(const WorldQuestTurnIn& value, std::vector<std::uint8_t>& out);
bool DecodeWorldQuestTurnIn(const std::uint8_t* data, std::size_t size,
                            WorldQuestTurnIn& out, std::string& error);
bool EncodeWorldQuestTurnInResult(const WorldQuestTurnInResult& value,
                                  std::vector<std::uint8_t>& out);
bool DecodeWorldQuestTurnInResult(const std::uint8_t* data, std::size_t size,
                                  WorldQuestTurnInResult& out, std::string& error);
bool EncodeWorldQuestAbandon(const WorldQuestAbandon& value, std::vector<std::uint8_t>& out);
bool DecodeWorldQuestAbandon(const std::uint8_t* data, std::size_t size,
                             WorldQuestAbandon& out, std::string& error);
bool EncodeWorldQuestObjective(const WorldQuestObjective& value, std::vector<std::uint8_t>& out);
bool DecodeWorldQuestObjective(const std::uint8_t* data, std::size_t size,
                               WorldQuestObjective& out, std::string& error);
bool EncodeWorldQuestState(const WorldQuestState& value, std::vector<std::uint8_t>& out);
bool DecodeWorldQuestState(const std::uint8_t* data, std::size_t size,
                           WorldQuestState& out, std::string& error);
bool EncodeWorldOfflineKill(const WorldOfflineKill& value, std::vector<std::uint8_t>& out);
bool DecodeWorldOfflineKill(const std::uint8_t* data, std::size_t size,
                            WorldOfflineKill& out, std::string& error);
bool EncodeWorldSavePosition(const WorldSavePosition& value, std::vector<std::uint8_t>& out);
bool DecodeWorldSavePosition(const std::uint8_t* data, std::size_t size,
                             WorldSavePosition& out, std::string& error);
bool EncodeWorldSaveProgression(const WorldSaveProgression& value, std::vector<std::uint8_t>& out);
bool DecodeWorldSaveProgression(const std::uint8_t* data, std::size_t size,
                                WorldSaveProgression& out, std::string& error);
bool EncodeWorldAddRewards(const WorldAddRewards& value, std::vector<std::uint8_t>& out);
bool DecodeWorldAddRewards(const std::uint8_t* data, std::size_t size,
                           WorldAddRewards& out, std::string& error);
bool EncodeWorldSaveGold(const WorldSaveGold& value, std::vector<std::uint8_t>& out);
bool DecodeWorldSaveGold(const std::uint8_t* data, std::size_t size,
                         WorldSaveGold& out, std::string& error);
bool EncodeWorldShopBuy(const WorldShopBuy& value, std::vector<std::uint8_t>& out);
bool DecodeWorldShopBuy(const std::uint8_t* data, std::size_t size,
                        WorldShopBuy& out, std::string& error);
bool EncodeWorldShopBuyResult(const WorldShopBuyResult& value, std::vector<std::uint8_t>& out);
bool DecodeWorldShopBuyResult(const std::uint8_t* data, std::size_t size,
                              WorldShopBuyResult& out, std::string& error);
bool EncodeWorldShopSell(const WorldShopSell& value, std::vector<std::uint8_t>& out);
bool DecodeWorldShopSell(const std::uint8_t* data, std::size_t size,
                         WorldShopSell& out, std::string& error);
bool EncodeWorldCharacterRow(const WorldCharacterRow& value, std::vector<std::uint8_t>& out);
bool DecodeWorldCharacterRow(const std::uint8_t* data, std::size_t size,
                             WorldCharacterRow& out, std::string& error);
bool EncodeWorldQuestStateList(const WorldQuestStateList& value, std::vector<std::uint8_t>& out);
bool DecodeWorldQuestStateList(const std::uint8_t* data, std::size_t size,
                               WorldQuestStateList& out, std::string& error);

} // namespace legend::internal
