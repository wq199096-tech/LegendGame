#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段13：服务器权威怪物常量（指令二/八/三十/四十一/六十七）
// ---------------------------------------------------------------------------

// 测试怪物类型（指令二）：阶段13 只做一种，硬编码（指令五）。
inline constexpr std::uint32_t kTrainingSlimeTypeId = 1;
inline constexpr const char* kTrainingSlimeName = "Training Slime";

// 初始怪物数量（指令十五/十六）：map1 固定 20 只，固定位置表（测试可复现）。
inline constexpr std::size_t kInitialMonsterCount = 20;

// AI Tick（指令三十/六十九）：200ms；dt clamp（指令四十一）最大 0.25s。
inline constexpr int kMonsterAiTickMs = 200;
inline constexpr float kMaxMonsterAiDeltaTime = 0.25f;

// 怪物 batch 上限（指令二十二）：单包最多 128。
inline constexpr std::size_t kMonsterBatchMaxMonsters = 128;

// Patrol 到达判定（指令三十五/四十六）：距离目标/spawn <=10 进入下一状态。
inline constexpr float kMonsterArriveDistance = 10.0f;

// Idle 持续（指令三十二）：阶段13 固定 2 秒后进入 Patrol。
inline constexpr float kMonsterIdleDurationSeconds = 2.0f;

// 怪物 AOI（指令二十三）：与玩家一致。
// EnterRadius=600 / LeaveRadius=700 复用 kAoiEnterRadius / kAoiLeaveRadius。

// 阶段13 指令八：MonsterState。
// 阶段14 指令五十二/五十三：正式加入 Dead（阶段13 曾禁止）。
enum class MonsterState : std::uint8_t {
    Idle = 0,
    Patrol = 1,
    Chase = 2,
    Returning = 3,
    Dead = 4,
};

inline const char* MonsterStateName(std::uint8_t state) {
    switch (static_cast<MonsterState>(state)) {
        case MonsterState::Idle: return "Idle";
        case MonsterState::Patrol: return "Patrol";
        case MonsterState::Chase: return "Chase";
        case MonsterState::Returning: return "Returning";
        case MonsterState::Dead: return "Dead";
    }
    return "Unknown";
}

// 阶段13 指令十九：MonsterDespawn.reason。
enum class MonsterDespawnReason : std::uint8_t {
    LeftAOI = 1,
    Removed = 2,
    ChangedMap = 3,
    ServerCleanup = 4,
};

// 阶段13 指令十五/十六：固定初始 spawn 表（5 簇 x 4 只，全部 0~2000 内）。
struct MonsterSpawnPoint {
    float x;
    float y;
};
inline constexpr MonsterSpawnPoint kInitialMonsterSpawnTable[kInitialMonsterCount] = {
    {500.0f, 500.0f},   {700.0f, 500.0f},   {500.0f, 700.0f},   {700.0f, 700.0f},
    {1000.0f, 500.0f},  {1200.0f, 500.0f},  {1000.0f, 700.0f},  {1200.0f, 700.0f},
    {1500.0f, 500.0f},  {1700.0f, 500.0f},  {1500.0f, 700.0f},  {1700.0f, 700.0f},
    {500.0f, 1500.0f},  {700.0f, 1500.0f},  {500.0f, 1700.0f},  {700.0f, 1700.0f},
    {1500.0f, 1500.0f}, {1700.0f, 1500.0f}, {1500.0f, 1700.0f}, {1700.0f, 1700.0f},
};

} // namespace legend::world
