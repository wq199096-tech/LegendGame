#pragma once

#include "Server/WorldServer/AOI/WorldSpatialGrid.h"
#include "Shared/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Server/WorldServer/WorldManager.h"

namespace legend::world {

// 阶段13 指令三十六~三十九：Aggro 目标选择——aggroRadius 内最近玩家，
// 同距离 characterId 最小（指令三十八）。查询经玩家 SpatialGrid（指令三十七）。
// 独立函数便于纯逻辑测试（AggroNearest/AggroStableTie）。
std::shared_ptr<PlayerSession> SelectAggroTarget(const MonsterEntity& monster,
                                                 const MonsterDefinition& definition,
                                                 const WorldSpatialGrid& playerGrid);

// 阶段13 指令三十二~四十八：单只怪物 AI 状态机推进一步。
// 仅在 World io 线程调用（指令三十一：不建 AI 线程池）；
// AI 与 AOI 解耦（指令六十八）：本函数只管状态与位置，不管谁能看到怪物。
// 状态机（阶段14 指令五十二/五十三：Dead 正式加入）：
//   Idle(2s) -> Patrol(spawn 附近 <=180 确定性目标点) -> 到达 -> Idle
//   Idle/Patrol 中 aggro(350) 内有玩家 -> Chase（最近优先，同距离 characterId 最小）
//   Chase：目标断线/死亡 / 离 spawn>600(Leash) / 目标>525(aggro*1.5) -> Returning
//   Chase：距目标 <= attackRange(60) 停止移动（阶段14 指令四十三/四十四；
//          攻击由 WorldServer AI tick 统一触发，指令四十五）
//   Returning：回 spawn，到达(<=10) -> Idle(target=0)；回途不做 aggro 扫描。
//   Dead：AI 直接跳过（阶段14 指令五十六；位置冻结，指令一百一十三）。
// 移动：直线朝目标（无寻路/碰撞，指令四十八），速度 = moveSpeed * dt（dt clamp 0.25s，指令四十一），
//       位置 Clamp 0~2000（指令四十七）。
void StepMonsterAi(MonsterEntity& monster, const MonsterDefinition& definition, float deltaTime,
                   const WorldSpatialGrid& playerGrid, const WorldManager& players);

} // namespace legend::world
