#pragma once

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 规则边界说明（StatusEffectError.h）：
//
// - 所有状态效果 100% WorldServer 权威（指令十七）：Client 不能发送
//   ApplyStatusRequest；状态只能由 WorldServer 技能逻辑或测试白盒施加。
// - 同一 effectId 在同一目标只允许一个 Active 实例（指令十四）。
// - 多玩家对同一目标施同一效果：后施加者覆盖 source（sourceEntityId/
//   sourceSkillId 更新为最新施加者），README 写明（指令七十二）。
// - DOT Tick 权威规则（指令四十二）：now >= nextTick 且 nextTick <= expireTime
//   则执行 Tick；然后 now >= expireTime 才移除。保证 Burn 产生 2/4/6/8s 四跳、
//   Poison 1/2/3/4/5/6s 六跳。
// - Tick Catch-up（指令四十三）：一次 Status Tick 最多补 3 次 DOT Tick，超出
//   部分把 nextTick 推进到未来，避免卡顿后瞬间几十跳。
// - AddStackRefresh 刷新 expireTime 但不修改 nextTickTime（指令四十七）。
// - RefreshDuration 的 Burn 重复施加重置 nextTick = now + tickInterval（四十六）。
// - 技能伤害直接杀死目标时不再施加状态（指令六十九）。
// - 状态 runtime-only（指令七十九）：不写 DB，WorldServer 重启全部清空。
// ---------------------------------------------------------------------------

} // namespace legend::world
