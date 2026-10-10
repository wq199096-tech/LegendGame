#pragma once

#include <cstdint>

namespace legend::network {

// 阶段9 指令十二：MessageId 枚举（阶段9 最小集合 + 阶段10 Account 集合）。
enum class MessageId : std::uint16_t {
    None = 0,
    ClientHello = 1,
    ServerHello = 2,
    // 阶段9：LegacyDevLogin（阶段10 保留兼容，正式流程改用 AccountLoginRequest）
    LoginRequest = 10,
    LoginResponse = 11,
    HeartbeatPing = 20,
    HeartbeatPong = 21,
    DisconnectNotice = 30,
    // 阶段10 指令十九：Account / Character
    RegisterRequest = 40,
    RegisterResponse = 41,
    AccountLoginRequest = 42,
    AccountLoginResponse = 43,
    CharacterListRequest = 50,
    CharacterListResponse = 51,
    CharacterCreateRequest = 52,
    CharacterCreateResponse = 53,
    CharacterDeleteRequest = 54,
    CharacterDeleteResponse = 55,
    CharacterSelectRequest = 56,
    CharacterSelectResponse = 57,
    SessionResumeRequest = 60,
    SessionResumeResponse = 61,
    // 阶段9 内部（Gateway <-> LoginServer）
    GatewayLoginForward = 100,
    LoginGatewayResponse = 101,
    // 阶段10 内部（Gateway <-> LoginServer 账号信封，Client 不感知）
    GatewayAccountForward = 102,
    GatewayAccountResponse = 103,
    // 阶段11 内部（WorldServer <-> LoginServer，Ticket 一次性消费）
    ConsumeSelectionTicketRequest = 120,
    ConsumeSelectionTicketResponse = 121,
    // 阶段11：Client <-> WorldServer
    WorldClientHello = 200,
    WorldServerHello = 201,
    EnterWorldRequest = 210,
    EnterWorldResponse = 211,
    WorldDisconnectNotice = 212,
    // 阶段26 指令十七：主动离开世界回角色大厅（保存→移除→响应；区别于断线）。
    LeaveWorldRequest = 213,
    LeaveWorldResponse = 214,
    PlayerMoveInput = 220,
    PlayerPositionSnapshot = 221,
    // 阶段12 指令二十一：AOI 多玩家同步
    PlayerSpawn = 230,
    PlayerDespawn = 231,
    RemotePlayerSnapshot = 232,
    RemotePlayerBatchSnapshot = 233,
    // 阶段13 指令十七：服务器权威怪物
    MonsterSpawn = 240,
    MonsterDespawn = 241,
    MonsterBatchSnapshot = 242,
    // 阶段14 指令十：服务器权威战斗（PlayerDeath 用 256——255 已被 ErrorResponse
    // 占用，指令十允许微调编号不冲突）
    PlayerAttackRequest = 250,
    PlayerAttackResponse = 251,
    CombatEvent = 252,
    EntityHealthSnapshot = 253,
    MonsterDeath = 254,
    PlayerDeath = 256,
    // 阶段15 指令二十二：Skill & Ability Replication（编号允许调整不冲突）
    SkillCastRequest = 260,
    SkillCastResponse = 261,
    SkillCastStarted = 262,
    SkillCastCompleted = 263,
    SkillCastCancelled = 264,
    SkillImpactEvent = 265,
    ManaSnapshot = 266,
    // 阶段16 指令五十一：Status Effect（编号允许微调不冲突）
    StatusEffectApplied = 270,
    StatusEffectUpdated = 271,
    StatusEffectRemoved = 272,
    StatusEffectSnapshot = 273,
    // 阶段17：成长/奖励（服务器权威，Client 只接收）。
    RewardGranted = 280,
    LevelUpEvent = 281,
    ProgressionSnapshot = 282,
    // 阶段18：服务器权威物品/掉落/背包/装备（290~300）。
    WorldItemSpawn = 290,
    WorldItemDespawn = 291,
    ItemPickupRequest = 292,
    ItemPickupResponse = 293,
    InventorySnapshot = 294,
    InventoryDelta = 295,
    EquipItemRequest = 296,
    EquipItemResponse = 297,
    UnequipItemRequest = 298,
    UnequipItemResponse = 299,
    EquipmentSnapshot = 300,
    // 阶段19：服务器权威任务（310~319；Client 只能发 310/312/314 三个请求）。
    QuestAcceptRequest = 310,
    QuestAcceptResponse = 311,
    QuestTurnInRequest = 312,
    QuestTurnInResponse = 313,
    QuestAbandonRequest = 314,
    QuestAbandonResponse = 315,
    QuestProgressUpdated = 316,
    QuestStateChanged = 317,
    QuestSnapshot = 318,
    QuestRewardGranted = 319,
    // 阶段20：NPC / Dialogue / Shop / Teleport（320~334）。
    NpcSpawn = 320,
    NpcDespawn = 321,
    NpcInteractRequest = 322,
    NpcInteractResponse = 323,
    DialogueOptionRequest = 324,
    DialoguePayload = 325,
    NpcQuestMarkerUpdate = 326,
    ShopOpenRequest = 327,
    ShopOpenResponse = 328,
    ShopBuyRequest = 329,
    ShopBuyResponse = 330,
    ShopSellRequest = 331,
    ShopSellResponse = 332,
    TeleportRequest = 333,
    TeleportResponse = 334,
    // 阶段21：多地图 / 传送门 / 复活（340~348）。
    PortalSpawn = 340,
    PortalDespawn = 341,
    PortalUseRequest = 342,
    PortalUseResponse = 343,
    MapChanged = 344,
    MapSnapshot = 345,
    RespawnRequest = 346,
    RespawnResponse = 347,
    PlayerRespawned = 348,
    // Stage27 指令十三：聊天（350~352；349 留空作安全间隔。此前游戏消息段最大
    // PlayerRespawned=348，1000+ 为 Stage25.5 内部协议段，客户端永不路由）。
    ChatSendRequest = 350,
    ChatSendResponse = 351,
    ChatMessageEvent = 352,
    // Stage25.5: private server-to-server protocol. Clients are never routed to these IDs.
    InternalServiceHandshake = 1000,
    InternalServiceHandshakeAck = 1001,
    InternalHeartbeat = 1002,
    InternalDbRequest = 1010,
    InternalDbResponse = 1011,
    InternalLogEvent = 1020,
    InternalLogAck = 1021,
    WorldErrorResponse = 2550,
    ErrorResponse = 255,
};

// 阶段10 指令五十四：Gateway 只转发 Account 消息，不解析业务 payload。
inline bool IsAccountMessageId(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::RegisterRequest:
        case MessageId::AccountLoginRequest:
        case MessageId::CharacterListRequest:
        case MessageId::CharacterCreateRequest:
        case MessageId::CharacterDeleteRequest:
        case MessageId::CharacterSelectRequest:
        case MessageId::SessionResumeRequest:
            return true;
        default:
            return false;
    }
}

// 阶段10 指令一百零二：写请求（同 Session 未完成时拒绝重复）。
inline bool IsAccountWriteMessageId(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::RegisterRequest:
        case MessageId::AccountLoginRequest:
        case MessageId::CharacterCreateRequest:
        case MessageId::CharacterDeleteRequest:
        case MessageId::CharacterSelectRequest:
        case MessageId::SessionResumeRequest:
            return true;
        default:
            return false;
    }
}

// Account 请求 -> 对应 Response MessageId（Gateway 超时合成错误响应用）。
inline std::uint16_t AccountResponseMessageId(std::uint16_t requestId) {
    switch (static_cast<MessageId>(requestId)) {
        case MessageId::RegisterRequest: return static_cast<std::uint16_t>(MessageId::RegisterResponse);
        case MessageId::AccountLoginRequest: return static_cast<std::uint16_t>(MessageId::AccountLoginResponse);
        case MessageId::CharacterListRequest: return static_cast<std::uint16_t>(MessageId::CharacterListResponse);
        case MessageId::CharacterCreateRequest: return static_cast<std::uint16_t>(MessageId::CharacterCreateResponse);
        case MessageId::CharacterDeleteRequest: return static_cast<std::uint16_t>(MessageId::CharacterDeleteResponse);
        case MessageId::CharacterSelectRequest: return static_cast<std::uint16_t>(MessageId::CharacterSelectResponse);
        case MessageId::SessionResumeRequest: return static_cast<std::uint16_t>(MessageId::SessionResumeResponse);
        default: return 0;
    }
}

// 阶段9 指令七十：未知 messageId 视为 Protocol Error（不要静默忽略）。
inline bool IsValidMessageId(std::uint16_t id) {
    switch (static_cast<MessageId>(id)) {
        case MessageId::ClientHello:
        case MessageId::ServerHello:
        case MessageId::LoginRequest:
        case MessageId::LoginResponse:
        case MessageId::HeartbeatPing:
        case MessageId::HeartbeatPong:
        case MessageId::DisconnectNotice:
        case MessageId::RegisterRequest:
        case MessageId::RegisterResponse:
        case MessageId::AccountLoginRequest:
        case MessageId::AccountLoginResponse:
        case MessageId::CharacterListRequest:
        case MessageId::CharacterListResponse:
        case MessageId::CharacterCreateRequest:
        case MessageId::CharacterCreateResponse:
        case MessageId::CharacterDeleteRequest:
        case MessageId::CharacterDeleteResponse:
        case MessageId::CharacterSelectRequest:
        case MessageId::CharacterSelectResponse:
        case MessageId::SessionResumeRequest:
        case MessageId::SessionResumeResponse:
        case MessageId::GatewayLoginForward:
        case MessageId::LoginGatewayResponse:
        case MessageId::GatewayAccountForward:
        case MessageId::GatewayAccountResponse:
        case MessageId::ConsumeSelectionTicketRequest:
        case MessageId::ConsumeSelectionTicketResponse:
        case MessageId::WorldClientHello:
        case MessageId::WorldServerHello:
        case MessageId::EnterWorldRequest:
        case MessageId::EnterWorldResponse:
        case MessageId::WorldDisconnectNotice:
        case MessageId::PlayerMoveInput:
        case MessageId::PlayerPositionSnapshot:
        case MessageId::PlayerSpawn:
        case MessageId::PlayerDespawn:
        case MessageId::RemotePlayerSnapshot:
        case MessageId::RemotePlayerBatchSnapshot:
        case MessageId::MonsterSpawn:
        case MessageId::MonsterDespawn:
        case MessageId::MonsterBatchSnapshot:
        case MessageId::PlayerAttackRequest:
        case MessageId::PlayerAttackResponse:
        case MessageId::CombatEvent:
        case MessageId::EntityHealthSnapshot:
        case MessageId::MonsterDeath:
        case MessageId::PlayerDeath:
        case MessageId::SkillCastRequest:
        case MessageId::SkillCastResponse:
        case MessageId::SkillCastStarted:
        case MessageId::SkillCastCompleted:
        case MessageId::SkillCastCancelled:
        case MessageId::SkillImpactEvent:
        case MessageId::ManaSnapshot:
        case MessageId::StatusEffectApplied:
        case MessageId::StatusEffectUpdated:
        case MessageId::StatusEffectRemoved:
        case MessageId::StatusEffectSnapshot:
        case MessageId::RewardGranted:
        case MessageId::LevelUpEvent:
        case MessageId::ProgressionSnapshot:
        case MessageId::WorldItemSpawn:
        case MessageId::WorldItemDespawn:
        case MessageId::ItemPickupRequest:
        case MessageId::ItemPickupResponse:
        case MessageId::InventorySnapshot:
        case MessageId::InventoryDelta:
        case MessageId::EquipItemRequest:
        case MessageId::EquipItemResponse:
        case MessageId::UnequipItemRequest:
        case MessageId::UnequipItemResponse:
        case MessageId::EquipmentSnapshot:
        case MessageId::QuestAcceptRequest:
        case MessageId::QuestAcceptResponse:
        case MessageId::QuestTurnInRequest:
        case MessageId::QuestTurnInResponse:
        case MessageId::QuestAbandonRequest:
        case MessageId::QuestAbandonResponse:
        case MessageId::QuestProgressUpdated:
        case MessageId::QuestStateChanged:
        case MessageId::QuestSnapshot:
        case MessageId::QuestRewardGranted:
        case MessageId::NpcSpawn:
        case MessageId::NpcDespawn:
        case MessageId::NpcInteractRequest:
        case MessageId::NpcInteractResponse:
        case MessageId::DialogueOptionRequest:
        case MessageId::DialoguePayload:
        case MessageId::NpcQuestMarkerUpdate:
        case MessageId::ShopOpenRequest:
        case MessageId::ShopOpenResponse:
        case MessageId::ShopBuyRequest:
        case MessageId::ShopBuyResponse:
        case MessageId::ShopSellRequest:
        case MessageId::ShopSellResponse:
        case MessageId::TeleportRequest:
        case MessageId::TeleportResponse:
        case MessageId::PortalSpawn:
        case MessageId::PortalDespawn:
        case MessageId::PortalUseRequest:
        case MessageId::PortalUseResponse:
        case MessageId::MapChanged:
        case MessageId::MapSnapshot:
        case MessageId::RespawnRequest:
        case MessageId::RespawnResponse:
        case MessageId::PlayerRespawned:
        case MessageId::ChatSendRequest:
        case MessageId::ChatSendResponse:
        case MessageId::ChatMessageEvent:
        case MessageId::InternalServiceHandshake:
        case MessageId::InternalServiceHandshakeAck:
        case MessageId::InternalHeartbeat:
        case MessageId::InternalDbRequest:
        case MessageId::InternalDbResponse:
        case MessageId::InternalLogEvent:
        case MessageId::InternalLogAck:
        case MessageId::WorldErrorResponse:
        case MessageId::ErrorResponse:
            return true;
        case MessageId::None:
            return false;
    }
    return false;
}

const char* MessageIdName(std::uint16_t id);

} // namespace legend::network
