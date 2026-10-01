#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::internal {

inline constexpr std::uint16_t kInternalProtocolVersion = 1;
inline constexpr std::size_t kMaxInternalPayloadBytes = 1024 * 1024;
inline constexpr std::size_t kMaxLogExtraBytes = 16 * 1024;

enum class ServiceType : std::uint16_t {
    Unknown = 0,
    LoginServer = 1,
    CharacterServer = 2,
    Gateway = 3,
    WorldServer = 4,
    DbServer = 5,
    LogServer = 6,
};

enum class ServiceHealth : std::uint8_t {
    Healthy = 1,
    Degraded = 2,
    Unavailable = 3,
};

enum class InternalErrorCode : std::uint16_t {
    Ok = 0,
    ServiceUnavailable = 1,
    DatabaseUnavailable = 2,
    NotAuthenticated = 3,
    CharacterNotFound = 4,
    CharacterOwnershipMismatch = 5,
    PersistenceConflict = 6,
    InvalidRequest = 7,
    Timeout = 8,
    ProtocolMismatch = 9,
    InternalError = 10,
    // 阶段25.5：账号/密码错误（保留语义供上层注册回退等流程使用）。
    InvalidCredentials = 11,
};

enum class DbOperation : std::uint16_t {
    LoadAccount = 1,
    SaveAccount = 2,
    LoadCharacterList = 3,
    CreateCharacter = 4,
    DeleteCharacter = 5,
    LoadCharacterState = 6,
    SaveCharacterState = 7,
    LoadInventory = 8,
    SaveInventory = 9,
    LoadEquipment = 10,
    SaveEquipment = 11,
    LoadQuestState = 12,
    SaveQuestState = 13,
    LoadPosition = 14,
    SavePosition = 15,
    LoadProgression = 16,
    SaveProgression = 17,
    ValidateSession = 18,
    // 阶段25.5：World 持久化 RPC（事务在 DbServer 内执行；World io 线程零 SQLite）。
    ItemInsertWrite = 19,      // 拾取/入包（Insert 新堆叠 或 Merge 既有堆叠）
    EquipItemWrite = 20,       // 装备原子事务（背包 -> character_equipment）
    UnequipItemWrite = 21,     // 卸下原子事务（character_equipment -> 背包）
    QuestInsertWrite = 22,     // 接取 UPSERT（state/时间戳重置 + 目标进度清零重建）
    QuestTurnInWrite = 23,     // TurnIn 原子事务（任务 Completed + 成长 + 奖励物品）
    QuestAbandonWrite = 24,    // 弃置（state=Abandoned + 进度清零）
    QuestObjectiveWrite = 25,  // 目标进度更新
    QuestStateWrite = 26,      // 任务状态更新（ReadyToTurnIn 等）
    OfflineKillWrite = 27,     // 离线 killer Kill 目标推进
    ShopBuyWrite = 28,         // 商店购买原子事务（Gold 扣除 + Inventory 写入）
    ShopSellWrite = 29,        // 商店出售原子事务（Inventory 扣除 + Gold 增加）
    SaveGoldWrite = 30,        // 金币扣费/入账（Portal/Respawn/NPC Teleport）
    AddRewardsWrite = 31,      // 离线 killer 奖励累加（角色已删除边界保底）
    LoadCharacterFull = 32,    // EnterWorld 全量角色行（含 map/pos/exp/gold/deleted）
};

enum class LogEventType : std::uint16_t {
    LoginSuccess = 1,
    LoginFailure = 2,
    CharacterCreate = 3,
    CharacterDelete = 4,
    CharacterSelect = 5,
    WorldEnter = 6,
    WorldLeave = 7,
    ShopBuy = 8,
    ShopSell = 9,
    QuestComplete = 10,
    ServerError = 11,
    AdminAction = 12,
};

struct ServiceHandshake {
    ServiceType serviceType = ServiceType::Unknown;
    std::uint16_t protocolVersion = kInternalProtocolVersion;
    std::string instanceId;
    std::string serviceToken;
};

struct ServiceHandshakeAck {
    bool accepted = false;
    InternalErrorCode errorCode = InternalErrorCode::Ok;
    std::string message;
};

struct Heartbeat {
    std::uint64_t sequence = 0;
    std::uint64_t timestampMs = 0;
    ServiceHealth health = ServiceHealth::Unavailable;
};

struct DbRequest {
    std::uint64_t requestId = 0;
    DbOperation operation = DbOperation::LoadAccount;
    std::uint64_t expectedVersion = 0;
    std::vector<std::uint8_t> payload;
};

struct DbResponse {
    std::uint64_t requestId = 0;
    InternalErrorCode errorCode = InternalErrorCode::Ok;
    std::uint64_t recordVersion = 0;
    std::string message;
    std::vector<std::uint8_t> payload;
};

struct LogEvent {
    std::uint64_t timestampMs = 0;
    ServiceType service = ServiceType::Unknown;
    std::uint16_t level = 0;
    LogEventType eventType = LogEventType::ServerError;
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::string message;
    std::string extraJson;
};

bool IsValidServiceType(ServiceType value);
bool IsValidDbOperation(DbOperation value);
bool IsValidLogEventType(LogEventType value);
const char* ServiceTypeName(ServiceType value);
const char* LogEventTypeName(LogEventType value);

bool EncodeServiceHandshake(const ServiceHandshake& value, std::vector<std::uint8_t>& out);
bool DecodeServiceHandshake(const std::uint8_t* data, std::size_t size,
                            ServiceHandshake& out, std::string& error);
bool EncodeServiceHandshakeAck(const ServiceHandshakeAck& value, std::vector<std::uint8_t>& out);
bool DecodeServiceHandshakeAck(const std::uint8_t* data, std::size_t size,
                               ServiceHandshakeAck& out, std::string& error);
bool EncodeHeartbeat(const Heartbeat& value, std::vector<std::uint8_t>& out);
bool DecodeHeartbeat(const std::uint8_t* data, std::size_t size,
                     Heartbeat& out, std::string& error);
bool EncodeDbRequest(const DbRequest& value, std::vector<std::uint8_t>& out);
bool DecodeDbRequest(const std::uint8_t* data, std::size_t size,
                     DbRequest& out, std::string& error);
bool EncodeDbResponse(const DbResponse& value, std::vector<std::uint8_t>& out);
bool DecodeDbResponse(const std::uint8_t* data, std::size_t size,
                      DbResponse& out, std::string& error);
bool EncodeLogEvent(const LogEvent& value, std::vector<std::uint8_t>& out);
bool DecodeLogEvent(const std::uint8_t* data, std::size_t size,
                    LogEvent& out, std::string& error);

bool ContainsSensitiveLogField(const std::string& message, const std::string& extraJson);

} // namespace legend::internal
