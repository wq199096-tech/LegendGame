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
