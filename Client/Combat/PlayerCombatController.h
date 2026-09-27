#pragma once

#include "Engine/Combat/CombatTarget.h"

namespace legend::combat {
class CombatSystem;
}

namespace legend::entity {
class ActorRegistry;
}

namespace legend::input {
class InputManager;
}

namespace legend::render {
class Camera2D;
}

// PlayerCharacter 为全局命名空间类（阶段3遗留），前置声明放在全局
class PlayerCharacter;

namespace legend::world {

// 玩家战斗控制器：目标选择（鼠标点击 / TAB 最近怪）+ 普通攻击（Space）+ 攻击/受击状态推进。
// 属性来自 character.json combat 块（不硬编码）；伤害统一走 CombatSystem。
class PlayerCombatController {
public:
    // 每帧：冷却递减 + 目标选择输入 + Attacking 事件消费 + 状态推进
    void Update(PlayerCharacter& player, const legend::entity::ActorRegistry& registry,
                legend::combat::CombatSystem& combat, const legend::input::InputManager& input,
                float viewportWidth, float viewportHeight, float deltaTime);

    // 请求普通攻击：校验 alive / Normal / 冷却 / 目标有效 / 攻击距离
    bool RequestAttack(PlayerCharacter& player, const legend::entity::ActorRegistry& registry,
                       legend::combat::CombatSystem& combat);

    // TAB：选择最近 alive Monster（限 1000 world units）
    void SelectNearestMonster(PlayerCharacter& player,
                              const legend::entity::ActorRegistry& registry);
    // 鼠标左键：命中怪物视觉 AABB -> 选 feet 最近者；点空地 -> 清目标
    void HandleClickSelection(PlayerCharacter& player,
                              const legend::entity::ActorRegistry& registry,
                              const legend::render::Camera2D& camera, float viewportWidth,
                              float viewportHeight);

    legend::combat::CombatTarget& GetTarget() { return m_target; }
    const legend::combat::CombatTarget& GetTarget() const { return m_target; }

private:
    void ConsumeAttackEvent(PlayerCharacter& player, const legend::entity::ActorRegistry& registry,
                            legend::combat::CombatSystem& combat);

    legend::combat::CombatTarget m_target;
};

} // namespace legend::world
