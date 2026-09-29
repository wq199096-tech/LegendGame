#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段19 指令五~八/二十一/二十二/四十六/八十三：Quest 公共类型（Shared 层，
// Server/Client 共用）。全部数值语义 100% WorldServer 权威（指令二）；
// Client 只是镜像展示（指令四十九：不能本地杀怪 +1）。
// ---------------------------------------------------------------------------

// QuestId（指令五）：uint32_t；阶段19 固定 4001~4005。
using QuestId = std::uint32_t;

// 任务状态（指令六）。
enum class QuestState : std::uint8_t {
    NotAccepted = 0,
    InProgress = 1,
    ReadyToTurnIn = 2,
    Completed = 3,
    Abandoned = 4,
};

const char* QuestStateName(std::uint8_t state);

// 目标类型（指令七）。
enum class QuestObjectiveType : std::uint8_t {
    KillMonster = 1,
    CollectItem = 2,
    ReachLevel = 3,
    ReachArea = 4,
};

const char* QuestObjectiveTypeName(std::uint8_t type);

// 同时进行中任务上限（指令二十一）：20；Completed 不算进行中。
inline constexpr std::size_t kMaxActiveQuests = 20;

// QuestSnapshot 上限（指令四十六/八十三）：256 Quest / 每 Quest 16 Objective，
// Decode 严格拒绝超限。
inline constexpr std::size_t kQuestSnapshotMaxQuests = 256;
inline constexpr std::size_t kQuestMaxObjectives = 16;

// Quest 请求防重放历史长度（指令五十九）：最近 64 个成功 requestId
//（Accept/TurnIn/Abandon 统一 QuestRequestHistory）。
inline constexpr std::size_t kQuestRequestHistorySize = 64;

// QuestResultCode（指令二十二）。
enum class QuestResultCode : std::uint8_t {
    Success = 0,
    UnknownQuest = 1,
    AlreadyAccepted = 2,
    AlreadyCompleted = 3,
    LevelTooLow = 4,
    PrerequisiteNotMet = 5,
    QuestLogFull = 6,
    NotAccepted = 7,
    NotReady = 8,
    InventoryFull = 9,
    DuplicateRequest = 10,
    Dead = 11,
    MalformedRequest = 12,
    NotInWorld = 13,
    InternalError = 14,
};

// ---------------------------------------------------------------------------
// 阶段19：Quest 枚举名称映射（inline 定义于此——Client/Server 头文件即用，
// 无需链接额外翻译单元；QuestError.h 为兼容薄头）。
// ---------------------------------------------------------------------------
inline const char* QuestStateName(std::uint8_t state) {
    switch (static_cast<QuestState>(state)) {
        case QuestState::NotAccepted: return "NotAccepted";
        case QuestState::InProgress: return "InProgress";
        case QuestState::ReadyToTurnIn: return "ReadyToTurnIn";
        case QuestState::Completed: return "Completed";
        case QuestState::Abandoned: return "Abandoned";
    }
    return "Unknown";
}

inline const char* QuestObjectiveTypeName(std::uint8_t type) {
    switch (static_cast<QuestObjectiveType>(type)) {
        case QuestObjectiveType::KillMonster: return "KillMonster";
        case QuestObjectiveType::CollectItem: return "CollectItem";
        case QuestObjectiveType::ReachLevel: return "ReachLevel";
        case QuestObjectiveType::ReachArea: return "ReachArea";
    }
    return "Unknown";
}

inline const char* QuestResultCodeName(std::uint8_t code) {
    switch (static_cast<QuestResultCode>(code)) {
        case QuestResultCode::Success: return "Success";
        case QuestResultCode::UnknownQuest: return "UnknownQuest";
        case QuestResultCode::AlreadyAccepted: return "AlreadyAccepted";
        case QuestResultCode::AlreadyCompleted: return "AlreadyCompleted";
        case QuestResultCode::LevelTooLow: return "LevelTooLow";
        case QuestResultCode::PrerequisiteNotMet: return "PrerequisiteNotMet";
        case QuestResultCode::QuestLogFull: return "QuestLogFull";
        case QuestResultCode::NotAccepted: return "NotAccepted";
        case QuestResultCode::NotReady: return "NotReady";
        case QuestResultCode::InventoryFull: return "InventoryFull";
        case QuestResultCode::DuplicateRequest: return "DuplicateRequest";
        case QuestResultCode::Dead: return "Dead";
        case QuestResultCode::MalformedRequest: return "MalformedRequest";
        case QuestResultCode::NotInWorld: return "NotInWorld";
        case QuestResultCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

} // namespace legend::world
