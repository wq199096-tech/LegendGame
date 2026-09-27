#include "Client/Skill/PlayerSkillController.h"

#include <SDL3/SDL_scancode.h>

#include "Client/Character/PlayerCharacter.h"
#include "Engine/Combat/CombatTarget.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Character.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Skill/SkillResource.h"

namespace legend::skill {

void PlayerSkillController::Initialize(const SkillDatabase* database,
                                       const legend::entity::ActorRegistry* registry,
                                       legend::combat::CombatSystem* combat) {
    m_skillSystem.Initialize(database, registry, combat);
    m_cooldowns.ResetAll();
}

void PlayerSkillController::Update(PlayerCharacter& player,
                                   const legend::combat::CombatTarget& target,
                                   const legend::input::InputManager& input, float deltaTime) {
    // 1. 冷却递减（无负值，归零即 Ready）
    m_cooldowns.Update(deltaTime);

    // 2. 施法打断检测：Context 存活但 ActionState 已不是 SkillCasting
    //    （HitReact / Dead 打断，指令二十五/二十六）——Cancel：Mana 不返还、CD 继续
    if (m_skillSystem.HasActiveCast() &&
        player.GetActionState() != legend::entity::CharacterActionState::SkillCasting) {
        const char* reason = player.GetActionState() ==
                                     legend::entity::CharacterActionState::Dead
                                 ? "caster died"
                                 : "interrupted by HitReact";
        m_skillSystem.CancelCast(reason);
    } else if (m_skillSystem.HasActiveCast()) {
        // 3. 施法 Animation Event 路由（指令八十七/八十八）：只在 SkillCasting 状态消费；
        //    Attacking 的 attack_hit 由 PlayerCombatController 消费；HitReact 遗留事件由其
        //    丢弃——两系统绝不抢同一事件。先消费事件再判播完（同帧事件+结束不丢失）
        if (player.GetActionState() == legend::entity::CharacterActionState::SkillCasting) {
            auto events = player.GetAnimationPlayer().ConsumeEvents();
            for (const auto& eventName : events) {
                m_skillSystem.HandleAnimationEvent(player, eventName);
            }
        }
        // 4. 施法动画播完：SkillCasting -> Normal，Context 清空（指令四十八/七十一：
        //    skill_hit 触发后仍保持 SkillCasting 到动画结束，不提前回 Normal）
        if (player.GetAnimationPlayer().IsFinished()) {
            m_skillSystem.FinishCast(player);
        }
    }

    // 5. 技能栏按键 1~4（Slot 0~3；无 Target 的 SingleTarget 在 BeginCast 内拒绝，
    //    日志 [Skill] no target，不扣 MP 不 CD）
    for (int slot = 0; slot < player.GetLoadout().GetSlotCount(); ++slot) {
        const SDL_Scancode key = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + slot);
        if (input.IsKeyPressed(key)) {
            (void)RequestSkill(player, slot, target);
        }
    }

    // 6. M：Fill Mana（Debug 键，指令五十/五十一）
    if (input.IsKeyPressed(SDL_SCANCODE_M) && player.IsCombatEnabled()) {
        player.GetSkillResource().FillMana();
        LOG_INFO("[Skill] mana restored to " +
                 std::to_string(static_cast<int>(player.GetSkillResource().GetMana())) + "/" +
                 std::to_string(static_cast<int>(player.GetSkillResource().GetMaxMana())));
    }
}

SkillCastResult PlayerSkillController::RequestSkill(
    PlayerCharacter& player, int slotIndex, const legend::combat::CombatTarget& target) {
    const std::string& skillId = player.GetLoadout().GetSkillId(slotIndex);
    if (skillId.empty()) {
        LOG_INFO("[Skill] slot " + std::to_string(slotIndex + 1) + " empty.");
        return {false, "slot empty"};
    }
    auto result = m_skillSystem.BeginCast(player, skillId, target, m_cooldowns);
    if (!result.success && result.reason != "action state blocks casting" &&
        result.reason != "skill on cooldown" && result.reason != "slot empty") {
        // 常规拒绝（无目标/距离/MP不足）记一次日志；CD/状态拒绝在专用阶段验证时打
        LOG_INFO("[Skill] " + skillId + " rejected: " + result.reason);
    }
    return result;
}

void PlayerSkillController::ResetForRespawn(PlayerCharacter& player) {
    m_skillSystem.CancelCast("player respawn"); // 清施法（若有）
    m_cooldowns.ResetAll();                     // 所有技能 CD 清 0（指令八十）
    player.GetSkillResource().FillMana();       // Mana 恢复满（指令七十九）
}

} // namespace legend::skill
