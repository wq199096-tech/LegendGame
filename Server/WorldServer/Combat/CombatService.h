#pragma once

#include "Shared/Combat/CombatTypes.h"

#include <chrono>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段14 指令三十五/三十七：CombatService —— 战斗验证纯逻辑（无 IO/无状态）。
// WorldServer 负责编排（接收请求 -> 调用验证 -> 扣血 -> 发 Response -> 广播
// CombatEvent，指令三十九）；WorldSession 只做协议/状态机（指令三十八）。
// 验证全部基于服务器权威数据（位置/冷却/alive 均为 WorldServer steady_clock 与
// PlayerSession/MonsterEntity 状态），绝不信 Client（指令二十七）。
// ---------------------------------------------------------------------------

// 指令三十七：一次攻击的验证上下文（纯数据，纯函数可测——DifferentMap 等真实链路
// 无法构造的场景在此覆盖，指令一百二十）。
struct AttackContext {
    bool attackerAlive = true;
    bool attackerInWorld = true;  // PlayerSession 存在于 WorldManager
    bool targetAlive = true;
    bool targetVisible = true;    // 目标在攻击者 visibleMonsters（指令三十二）
    bool sameMap = true;          // 指令二十：不同地图绝不能攻击
    float distanceSquared = 0.0f; // 服务器权威位置（指令十八/三十三）
    float attackRangeSquared = 0.0f;
    std::chrono::steady_clock::time_point now{};
    std::chrono::steady_clock::time_point lastAttackTime{};
    float attackCooldownSeconds = 0.0f;
};

// 指令三十七：验证链（顺序固定，短路返回首个失败）：
//   AttackerDead -> NotInWorld -> TargetDead -> InvalidTarget(不可见) ->
//   DifferentMap -> OutOfRange -> Cooldown -> Success
// targetType=Player 的拦截在 WorldServer 协议层（指令八十三：返回 InvalidTarget，
// 不进本函数——本函数只处理已确认为 Monster 的目标）。
CombatResultCode ValidateAttack(const AttackContext& context);

// 指令二十七/二十八/二十九/四十六：冷却判定（WorldServer steady_clock）。
bool IsAttackOffCooldown(std::chrono::steady_clock::time_point now,
                         std::chrono::steady_clock::time_point lastAttackTime,
                         float attackCooldownSeconds);

// 指令十八/十九：距离判定（distanceSquared，无 sqrt）。
bool IsWithinAttackRange(float distanceSquared, float attackRangeSquared);

} // namespace legend::world
