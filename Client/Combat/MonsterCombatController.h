#pragma once

#include "Engine/Combat/CombatTarget.h"

namespace legend::combat {
class CombatSystem;
}

namespace legend::entity {
class ActorRegistry;
}

namespace legend::world {
class MonsterCharacter;

// 怪物战斗控制器：普通攻击请求 / 攻击动画事件 -> 伤害 / 受击与死亡状态推进。
// AI 只决定"是否需要攻击"，实际伤害统一走 CombatSystem。
class MonsterCombatController {
public:
    // 每帧：冷却递减 + Attacking 事件消费 + HitReact/Attacking 播完恢复 Normal
    void Update(MonsterCharacter& monster, const legend::entity::ActorRegistry& registry,
                legend::combat::CombatSystem& combat, float deltaTime);

    // 请求普通攻击（AI 在 Chase 停步于攻击范围时调用）：
    // 校验 alive / Normal / 冷却 / 目标有效 / 攻击距离（DistanceSquared）
    bool RequestAttack(MonsterCharacter& monster, const legend::entity::ActorRegistry& registry,
                       legend::combat::CombatSystem& combat);

private:
    // 消费攻击动画事件：attack_hit -> 重新 Resolve 目标（防一帧延迟后目标已删） -> ResolveAttack
    void ConsumeAttackEvent(MonsterCharacter& monster,
                            const legend::entity::ActorRegistry& registry,
                            legend::combat::CombatSystem& combat);
};

} // namespace legend::world