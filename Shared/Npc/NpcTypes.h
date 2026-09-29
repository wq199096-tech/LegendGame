#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令五~八/九十：NPC 公共类型（Shared 层，Server/Client 共用）。
// NPC 100% 服务器权威（指令二）；阶段20 NPC 不移动/不死亡/不参与 Combat（指令八）。
// ---------------------------------------------------------------------------

// NpcDefinitionId（指令五）：uint32_t；阶段20 固定 5001~5004。
using NpcDefinitionId = std::uint32_t;

// NPC 类型（指令六）。
enum class NpcType : std::uint8_t {
    QuestGiver = 1,
    Merchant = 2,
    Teleporter = 3,
    MultiFunction = 4,
};

const char* NpcTypeName(std::uint8_t type);

// NpcDespawn 原因（指令十三）：阶段20 NPC 通常不消失。
enum class NpcDespawnReason : std::uint8_t {
    LeftAOI = 1,
    ServerCleanup = 2,
};

// NPC Quest Marker（指令三十一/三十二）：per-player（任务状态属于每个玩家）。
enum class NpcQuestMarker : std::uint8_t {
    None = 0,
    InProgress = 1,
    Available = 2,
    ReadyToTurnIn = 3, // 优先级最高
};

const char* NpcQuestMarkerName(std::uint8_t marker);

// NPC AOI（指令十一）：与玩家/怪物一致；SpatialGrid cellSize = 400。
inline constexpr float kNpcAoiEnterRadius = 600.0f;
inline constexpr float kNpcAoiLeaveRadius = 700.0f;
inline constexpr std::size_t kNpcSpatialCellSize = 400;

// 交互距离（指令九：interactionRange=120；指令十九：服务器权威距离校验）。
inline constexpr float kNpcDefaultInteractionRange = 120.0f;

// Dialogue Option 上限（指令二十六/九十）：定义在 Shared/Dialogue/DialogueTypes.h。
// Shop entries 上限（指令九十）：定义在 Shared/Shop/ShopTypes.h。

// Dialogue/Shop Session TTL（指令二十二/四十一）：30 秒。
inline constexpr double kNpcSessionTtlSeconds = 30.0;

// 购买数量限制（指令四十三）：1~99；装备 quantity 必须 1。
inline constexpr std::uint32_t kBuyQuantityMin = 1;
inline constexpr std::uint32_t kBuyQuantityMax = 99;

// NPC 请求防重放历史长度（指令五十七）：Interaction/Option/Buy/Sell/Teleport 统一 64。
inline constexpr std::size_t kNpcRequestHistorySize = 64;

// NpcResultCode（指令十九/二十/七十四）。
enum class NpcResultCode : std::uint8_t {
    Success = 0,
    MalformedRequest = 1,
    NotInWorld = 2,
    Dead = 3,
    NpcNotFound = 4,
    NpcNotActive = 5,
    WrongMap = 6,
    NotVisible = 7,
    TooFar = 8,
    DuplicateRequest = 9,
    SessionNotFound = 10,
    SessionExpired = 11,
    InternalError = 12,
};

const char* NpcResultCodeName(std::uint8_t code);

} // namespace legend::world
