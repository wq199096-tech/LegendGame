#pragma once

#include <string>

#include "Client/Skill/SkillSystem.h"
#include "Engine/Skill/SkillCooldowns.h"
#include "Engine/Skill/SkillLoadout.h"

namespace legend::input {
class InputManager;
}

// PlayerCharacter 为全局命名空间类（阶段3遗留），前置声明放在全局作用域
class PlayerCharacter;

namespace legend::combat {
class CombatTarget;
}

// 玩家技能控制器（阶段8指令四十九/一百零五）：
// 职责 = 读取 1~4 按键 + M（Fill Mana）/ 查询 SkillLoadout / 调用 SkillSystem::BeginCast /
//        每帧更新 Cooldown 与施法流程 / 按 ActionState 路由技能 Animation Event。
// GameScene 只调用 Update(...) —— 不出现 "if key1 then damage"。
// 事件路由（指令八十六~八十八）：SkillCasting 状态下技能事件由本控制器消费；
// Attacking 的 attack_hit 仍由 PlayerCombatController 消费；HitReact 遗留事件直接丢弃。
namespace legend::skill {

class PlayerSkillController {
public:
    // 依赖注入（GameScene OnLoad：WorldActorManager 初始化成功后调用；指针必须长期有效）
    void Initialize(const SkillDatabase* database, const legend::entity::ActorRegistry* registry,
                    legend::combat::CombatSystem* combat);

    // 每帧：CD 递减 -> 打断检测 -> 施法完成 -> 事件路由 -> 技能键/Debug键
    void Update(PlayerCharacter& player, const legend::combat::CombatTarget& target,
                const legend::input::InputManager& input, float deltaTime);

    // 释放指定技能栏（按键与自动测试共用同一入口；返回 BeginCast 结果）
    SkillCastResult RequestSkill(PlayerCharacter& player, int slotIndex,
                                 const legend::combat::CombatTarget& target);

    // Player Respawn 重置（阶段8指令七十九/八十）：Mana Fill + 所有 CD 清 0 + 施法清空
    void ResetForRespawn(PlayerCharacter& player);

    const SkillCooldowns& GetCooldowns() const { return m_cooldowns; }
    const SkillSystem& GetSkillSystem() const { return m_skillSystem; }
    SkillSystem& GetSkillSystem() { return m_skillSystem; } // 测试清理用（CancelCast）

private:
    SkillSystem m_skillSystem;
    SkillCooldowns m_cooldowns;
};

} // namespace legend::skill
