#pragma once

#include <string>

#include "Client/Skill/SkillCastContext.h"
#include "Engine/Combat/DamageEvent.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Skill/SkillCooldowns.h"
#include "Engine/Skill/SkillDatabase.h"

namespace legend::combat {
class CombatSystem;
class CombatTarget;
}

namespace legend::entity {
class ActorRegistry;
}

// PlayerCharacter 为全局命名空间类（阶段3遗留），前置声明放在全局作用域
class PlayerCharacter;

namespace legend::skill {

// 施法结果（BeginCast / CanCast 通用）
struct SkillCastResult {
    bool success = false;
    std::string reason; // 失败原因（日志用）
};

// 技能系统核心（阶段8指令三十三/一百零四）：
// 职责 = Definition 查询 / 施法合法性 / Mana 消耗 / CD 启动 / CastContext /
//        Animation Event 命中 / AOE 查询 / Damage 请求 / Cancel。
// 不负责：键盘输入 / Render / Inventory / Loot / Reward / Level / UI / 地图。
// 伤害统一走 CombatSystem::ApplySkillDamage（raw = attackSnapshot * multiplier，
// 防御折减由 CombatResolver 负责）——绝不直接 target.hp -= damage。
class SkillSystem {
public:
    // 依赖注入（GameScene OnLoad：WorldActorManager 初始化成功后调用；指针必须长期有效）
    void Initialize(const SkillDatabase* database, const legend::entity::ActorRegistry* registry,
                    legend::combat::CombatSystem* combat);

    // 纯校验（无副作用：不扣 MP / 不启动 CD / 不改状态）；failReason 输出失败原因
    SkillCastResult CanCast(const PlayerCharacter& player, const std::string& skillId,
                            const legend::combat::CombatTarget& target,
                            const SkillCooldowns& cooldowns) const;
    // 施法开始：全部校验通过后一次性 扣MP + 启动CD + 进入 SkillCasting + 锁方向 + 记快照
    SkillCastResult BeginCast(PlayerCharacter& player, const std::string& skillId,
                              const legend::combat::CombatTarget& target,
                              SkillCooldowns& cooldowns);

    // 动画事件命中（PlayerSkillController 按 ActionState 路由后调用）：
    // 比较 eventName == definition.animationEvent（不硬编码），eventConsumed 保证 exactly-once
    void HandleAnimationEvent(PlayerCharacter& player, const std::string& eventName);

    // 统一取消：清 Context + 取消未触发的 skill_hit；不返还 Mana、不清 CD（指令九十）
    void CancelCast(const std::string& reason);
    // 施法动画播完：SkillCasting -> Normal + Context 清空（指令四十八）
    void FinishCast(PlayerCharacter& player);

    bool HasActiveCast() const { return m_context.IsActive(); }
    const SkillCastContext& GetContext() const { return m_context; }

    // 最近一次 skill_hit 结算信息（Debug/自动验收观察用；不参与游戏逻辑）
    struct LastHitInfo {
        bool valid = false;
        std::string abilityId;
        float rawDamage = 0.0f;
        float finalDamage = 0.0f;
        int hitCount = 0;
    };
    const LastHitInfo& GetLastHit() const { return m_lastHit; }

private:
    // 事件触发结算：SingleTarget 重新 Resolve；SelfArea FindInRadius 筛选 Monster
    void ResolveSkillHit(PlayerCharacter& player);

    const SkillDatabase* m_database = nullptr;
    const legend::entity::ActorRegistry* m_registry = nullptr;
    legend::combat::CombatSystem* m_combat = nullptr;
    SkillCastContext m_context;
    LastHitInfo m_lastHit;
};

} // namespace legend::skill
