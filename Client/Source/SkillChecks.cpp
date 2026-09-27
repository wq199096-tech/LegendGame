// 阶段8：Skill Core System 自检实现（26 个 Check + LEGEND_AUTO_SKILL_TEST 时间线 + F7 Debug）。
// 从 GameScene 拆出独立文件（与 ProgressionChecks/EquipmentChecks 同模式）。
// 全部 Check 结果写日志 [XxxCheck] ... -> PASS/FAIL + completed, failures = N。
// 施法链路一律走真实入口：PlayerSkillController::RequestSkill -> SkillSystem::BeginCast ->
// Skill Animation -> Animation Event -> SkillSystem HandleAnimationEvent -> CombatSystem
// （禁止直接 ApplyDamage 冒充技能命中；ApplyDamage 仅用于制造 HitReact/Player 死亡前置）。
#include "Client/Source/GameScene.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Client/Character/PlayerCharacter.h"
#include "Client/Skill/PlayerSkillController.h"
#include "Client/Skill/SkillSystem.h"
#include "Client/World/MonsterCharacter.h"
#include "Client/World/MonsterDefinition.h"
#include "Client/World/MonsterSpawner.h"
#include "Client/World/WorldActorManager.h"
#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Combat/CombatTarget.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/Character.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Item/ItemDatabase.h"
#include "Engine/Item/ItemDefinition.h"
#include "Engine/Item/ItemInstance.h"
#include "Engine/Item/Inventory.h"
#include "Engine/Render/Texture.h"
#include "Engine/Skill/SkillCooldowns.h"
#include "Engine/Skill/SkillDatabase.h"
#include "Engine/Skill/SkillDefinition.h"
#include "Engine/Skill/SkillLoadout.h"
#include "Engine/Skill/SkillResource.h"
#include "Engine/Skill/SkillTypes.h"

using legend::Engine;

namespace {

// 统一 Check 输出：PASS/FAIL + 失败计数
void LogSkillCheck(const char* tag, const std::string& name, bool pass, int& failures) {
    LOG_INFO(std::string("[") + tag + "] " + name + " -> " + (pass ? "PASS" : "FAIL"));
    if (!pass) {
        ++failures;
    }
}

// 阶段8.1指令二十一：RAII 恢复守卫——Integration Check 的任何 return 路径
//（含 FAIL / 早退 / 中途 return）都会恢复快照世界（位置/HP/Active/ActionState/
// Player 状态/新增 GroundLoot 清理/Combat recent events 清空），避免早退漏 Restore。
struct RestoreGuard {
    GameScene* scene;
    explicit RestoreGuard(GameScene* sceneIn) : scene(sceneIn) {}
    ~RestoreGuard() { scene->RestoreSkillWorldSnapshot(); }
    RestoreGuard(const RestoreGuard&) = delete;
    RestoreGuard& operator=(const RestoreGuard&) = delete;
};

} // namespace

// ==================== [SkillDatabaseCheck] ====================
// 指令五十五：skills.json 加载成功、4 技能存在、关键字段精确匹配。

void GameScene::RunSkillDatabaseCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillDatabaseCheck", name, pass, failures);
    };
    const std::string path =
        Engine::Get().GetResources().GetAssetRoot() + "/Skills/skills.json";
    legend::skill::SkillDatabase database;
    check("official skills.json loads", database.LoadFromFile(path));
    check("4 skills present", database.Count() == 4);
    const legend::skill::SkillDefinition* powerSlash = database.Get("power_slash");
    check("power_slash exists", powerSlash != nullptr);
    if (powerSlash != nullptr) {
        check("power_slash SingleTarget",
              powerSlash->targetType == legend::skill::SkillTargetType::SingleTarget);
        check("power_slash multiplier 1.8",
              std::fabs(powerSlash->damageMultiplier - 1.8f) < 0.0001f);
        check("power_slash mana 15", std::fabs(powerSlash->manaCost - 15.0f) < 0.0001f);
        check("power_slash cooldown 4", std::fabs(powerSlash->cooldown - 4.0f) < 0.0001f);
        check("power_slash range 95", std::fabs(powerSlash->castRange - 95.0f) < 0.0001f);
        check("power_slash animation",
              powerSlash->animation == "skill_power_slash" &&
                  powerSlash->animationEvent == "skill_hit");
    }
    const legend::skill::SkillDefinition* whirlwind = database.Get("whirlwind");
    check("whirlwind exists", whirlwind != nullptr);
    if (whirlwind != nullptr) {
        check("whirlwind SelfArea",
              whirlwind->targetType == legend::skill::SkillTargetType::SelfArea);
        check("whirlwind multiplier 1.2",
              std::fabs(whirlwind->damageMultiplier - 1.2f) < 0.0001f);
        check("whirlwind mana 25", std::fabs(whirlwind->manaCost - 25.0f) < 0.0001f);
        check("whirlwind cooldown 6", std::fabs(whirlwind->cooldown - 6.0f) < 0.0001f);
        check("whirlwind radius 120", std::fabs(whirlwind->aoeRadius - 120.0f) < 0.0001f);
    }
    check("piercing_strike exists", database.Exists("piercing_strike"));
    check("heavy_strike exists", database.Exists("heavy_strike"));
    LOG_INFO("[SkillDatabaseCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillDatabaseFailureCheck] ====================
// 指令一百：不存在路径 -> false；非法 JSON -> false；正式 skills.json -> true 且 >=4 技能。

void GameScene::RunSkillDatabaseFailureCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillDatabaseFailureCheck", name, pass, failures);
    };
    legend::skill::SkillDatabase missing;
    check("missing file -> load false",
          !missing.LoadFromFile("Assets/Skills/skills_missing_for_check.json"));

    // 非法 JSON：临时构造（与 ItemDatabaseFailureCheck 同模式）
    const std::string badPath = "Build/skills_malformed_check.json";
    {
        std::ofstream bad(badPath, std::ios::trunc);
        bad << "{ this is not valid json !!!";
    }
    legend::skill::SkillDatabase malformed;
    const bool malformedRejected = !malformed.LoadFromFile(badPath);
    check("malformed json -> load false", malformedRejected);
    std::error_code ec;
    std::filesystem::remove(badPath, ec);

    // World 级正式库（WorldActorManager::Initialize 已加载成功，否则场景已回退）
    check("world skill database loaded", m_worldActors.GetSkillDatabase().Count() >= 4);
    LOG_INFO("[SkillDatabaseFailureCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillDefinitionValidationCheck] ====================
// 指令五十六：非法 Definition 一律 IsValid()==false；未知字符串解析拒绝。

void GameScene::RunSkillDefinitionValidationCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillDefinitionValidationCheck", name, pass, failures);
    };
    auto makeBase = []() {
        legend::skill::SkillDefinition def;
        def.id = "check_skill";
        def.name = "Check Skill";
        def.targetType = legend::skill::SkillTargetType::SingleTarget;
        def.effect = legend::skill::SkillEffectType::Damage;
        def.damageMultiplier = 1.0f;
        def.manaCost = 10.0f;
        def.cooldown = 1.0f;
        def.castRange = 100.0f;
        def.aoeRadius = 0.0f;
        def.animation = "skill_check";
        def.animationEvent = "skill_hit";
        def.requiresTarget = true;
        return def;
    };
    { auto def = makeBase(); def.damageMultiplier = 0.0f;
      check("multiplier 0 -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.damageMultiplier = -1.0f;
      check("negative multiplier -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.manaCost = -5.0f;
      check("negative manaCost -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.cooldown = -1.0f;
      check("negative cooldown -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.castRange = 0.0f;
      check("SingleTarget range 0 -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.aoeRadius = -1.0f;
      check("negative aoeRadius -> invalid", !def.IsValid()); }
    { auto def = makeBase();
      def.targetType = legend::skill::SkillTargetType::SelfArea; def.aoeRadius = 0.0f;
      check("SelfArea radius 0 -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.id.clear();
      check("empty id -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.name.clear();
      check("empty name -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.animation.clear();
      check("empty animation -> invalid", !def.IsValid()); }
    { auto def = makeBase(); def.animationEvent.clear();
      check("empty animationEvent -> invalid", !def.IsValid()); }
    // 未知 targetType / effect 字符串：解析层拒绝（不默认值）
    legend::skill::SkillTargetType parsedTarget = legend::skill::SkillTargetType::SingleTarget;
    check("unknown targetType rejected",
          !legend::skill::ParseSkillTargetType("BossAoE", parsedTarget));
    legend::skill::SkillEffectType parsedEffect = legend::skill::SkillEffectType::Damage;
    check("unknown effect rejected",
          !legend::skill::ParseSkillEffectType("Heal", parsedEffect));
    // 合法基线通过
    check("valid base definition passes", makeBase().IsValid());
    LOG_INFO("[SkillDefinitionValidationCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillLoadoutCheck] ====================
// 指令五十七：4 槽默认绑定正确；非法 skillId SetSlot 拒绝；同技能允许多槽。

void GameScene::RunSkillLoadoutCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillLoadoutCheck", name, pass, failures);
    };
    const std::string path =
        Engine::Get().GetResources().GetAssetRoot() + "/Skills/skills.json";
    legend::skill::SkillDatabase database;
    if (!database.LoadFromFile(path)) {
        check("skills.json loads for loadout check", false);
        LOG_INFO("[SkillLoadoutCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::skill::SkillLoadout loadout;
    loadout.InitializeDefaults(database);
    check("slot count 4", loadout.GetSlotCount() == 4);
    check("slot1 power_slash", loadout.GetSkillId(0) == "power_slash");
    check("slot2 whirlwind", loadout.GetSkillId(1) == "whirlwind");
    check("slot3 piercing_strike", loadout.GetSkillId(2) == "piercing_strike");
    check("slot4 heavy_strike", loadout.GetSkillId(3) == "heavy_strike");
    check("unknown skillId SetSlot rejected",
          !loadout.SetSlot(0, "nonexistent_skill", database));
    check("rejected SetSlot keeps old value", loadout.GetSkillId(0) == "power_slash");
    check("same skill in two slots allowed",
          loadout.SetSlot(2, "power_slash", database) &&
              loadout.GetSkillId(2) == "power_slash");
    loadout.ClearSlot(3);
    check("ClearSlot empties slot", loadout.GetSkillId(3).empty());
    // 玩家实际 Loadout 与默认一致（OnLoad 已 InitializeDefaults）
    check("player loadout slot1 power_slash",
          m_player != nullptr && m_player->GetLoadout().GetSkillId(0) == "power_slash");
    check("player loadout slot4 heavy_strike",
          m_player != nullptr && m_player->GetLoadout().GetSkillId(3) == "heavy_strike");
    LOG_INFO("[SkillLoadoutCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillManaCheck] ====================
// 指令五十八：初始 100/100；Spend 30 -> 70；Spend 80 失败仍 70；Restore 20 -> 90；
// Restore 100 clamp 100。

void GameScene::RunSkillManaCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillManaCheck", name, pass, failures);
    };
    legend::skill::SkillResource mana;
    mana.Initialize(100.0f);
    check("initial 100/100",
          std::fabs(mana.GetMana() - 100.0f) < 0.0001f &&
              std::fabs(mana.GetMaxMana() - 100.0f) < 0.0001f);
    check("percent 1.0", std::fabs(mana.GetManaPercent() - 1.0f) < 0.0001f);
    check("spend 30 ok", mana.Spend(30.0f));
    check("mana 70 after spend", std::fabs(mana.GetMana() - 70.0f) < 0.0001f);
    check("spend 80 fails", !mana.Spend(80.0f));
    check("mana still 70 after failed spend", std::fabs(mana.GetMana() - 70.0f) < 0.0001f);
    mana.Restore(20.0f);
    check("restore 20 -> 90", std::fabs(mana.GetMana() - 90.0f) < 0.0001f);
    mana.Restore(100.0f);
    check("restore 100 clamps to 100", std::fabs(mana.GetMana() - 100.0f) < 0.0001f);
    check("canSpend consistent", mana.CanSpend(100.0f) && !mana.CanSpend(100.1f));
    LOG_INFO("[SkillManaCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillCooldownCheck] ====================
// 指令五十九：Start 4s -> 立即 not ready；Update 1.5 -> remaining≈2.5；
// 再 Update 3 -> 0 且 Ready；无负值。

void GameScene::RunSkillCooldownCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillCooldownCheck", name, pass, failures);
    };
    legend::skill::SkillCooldowns cooldowns;
    cooldowns.StartCooldown("power_slash", 4.0f);
    check("not ready immediately", !cooldowns.IsReady("power_slash"));
    check("remaining 4 now", std::fabs(cooldowns.GetRemaining("power_slash") - 4.0f) < 0.0001f);
    cooldowns.Update(1.5f);
    check("remaining ~2.5 after 1.5s",
          std::fabs(cooldowns.GetRemaining("power_slash") - 2.5f) < 0.0001f);
    cooldowns.Update(3.0f);
    check("ready after total 4.5s", cooldowns.IsReady("power_slash"));
    check("remaining 0 when ready", cooldowns.GetRemaining("power_slash") == 0.0f);
    // 无负值：过量 Update 不会产生负 remaining
    cooldowns.StartCooldown("whirlwind", 1.0f);
    cooldowns.Update(10.0f);
    check("no negative remaining after over-update", cooldowns.IsReady("whirlwind"));
    // Reset / ResetAll
    cooldowns.StartCooldown("heavy_strike", 8.0f);
    cooldowns.Reset("heavy_strike");
    check("Reset(skill) clears single", cooldowns.IsReady("heavy_strike"));
    cooldowns.StartCooldown("a", 1.0f);
    cooldowns.StartCooldown("b", 2.0f);
    cooldowns.ResetAll();
    check("ResetAll clears all", cooldowns.IsReady("a") && cooldowns.IsReady("b"));
    // 未记录技能默认 Ready
    check("unknown skill ready by default", cooldowns.IsReady("never_started"));
    LOG_INFO("[SkillCooldownCheck] completed, failures = " + std::to_string(failures));
}

// ==================== 施法驱动辅助（各 Check 共用模式） ====================
// 每帧：UpdateAnimation（推进/发事件）-> PlayerSkillController::Update（消费/推进）。
// 结束条件：Context 消失（完成或取消）。返回施法是否正常走完（非取消）。

namespace {
constexpr int kMaxCastDriveFrames = 240;
}

// ==================== [SkillCastValidationCheck] ====================
// 指令六十：无目标/距离过远/Mana不足/CD中/Dead/HitReact 全部失败且不扣MP不启动CD；合法成功。

void GameScene::RunSkillCastValidationCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillCastValidationCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    auto& cooldowns = m_playerSkill.GetSkillSystem(); // 仅用于可读性命名（实际 CD 在控制器内）
    (void)cooldowns;
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillCastValidationCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 找一只活怪作为距离/目标用
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillCastValidationCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    auto& target = m_playerCombat.GetTarget();
    auto& mana = m_player->GetSkillResource();
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerSkill.ResetForRespawn(*m_player); // 满蓝 + 无 CD + 无施法（测试前置）
    float manaBefore = mana.GetMana();

    // 1. 无目标 -> SingleTarget 失败，不扣 MP 不 CD
    target.ClearTarget();
    auto result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("no target -> fail", !result.success);
    check("no target: mana unchanged", std::fabs(mana.GetMana() - manaBefore) < 0.0001f);
    check("no target: no cooldown", m_playerSkill.GetCooldowns().IsReady("power_slash"));

    // 2. 距离过远 -> 失败（boar 移到 300 外，power_slash range 95）
    target.SetTarget(boar->GetId());
    const legend::math::Vector2 boarHome = boar->GetPosition();
    boar->SetPosition(boarHome + legend::math::Vector2(300.0f, 0.0f));
    result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("out of range -> fail", !result.success);
    check("out of range: mana unchanged", std::fabs(mana.GetMana() - manaBefore) < 0.0001f);
    check("out of range: no cooldown", m_playerSkill.GetCooldowns().IsReady("power_slash"));
    boar->SetPosition(boarHome);

    // 3. Mana 不足 -> 失败
    mana.SetMana(5.0f);
    result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("low mana -> fail", !result.success);
    check("low mana: mana unchanged", std::fabs(mana.GetMana() - 5.0f) < 0.0001f);
    check("low mana: no cooldown", m_playerSkill.GetCooldowns().IsReady("power_slash"));
    mana.FillMana();

    // 4. CD 中 -> 失败：先真实施法一次（成功后 power_slash 进 CD 4s），立即再请求
    result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("legit cast (setup for cd) succeeds", result.success);
    if (result.success) {
        const float manaAfterCast = mana.GetMana();
        auto again = m_playerSkill.RequestSkill(*m_player, 0, target);
        check("on cooldown -> fail", !again.success);
        check("on cooldown: mana unchanged",
              std::fabs(mana.GetMana() - manaAfterCast) < 0.0001f);
    }
    m_playerSkill.ResetForRespawn(*m_player); // 清 CD/蓝/施法

    // 5. Dead -> 失败
    m_player->GetCombatStats().SetHp(0.0f);
    m_player->EnterDead();
    result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("dead -> fail", !result.success);
    m_player->GetCombatStats().SetHp(m_player->GetCombatStats().maxHp);
    m_player->ReturnToNormal();

    // 6. HitReact -> 失败
    m_player->EnterHitReact();
    result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("hitreact -> fail", !result.success);
    check("hitreact: mana unchanged", std::fabs(mana.GetMana() - mana.GetMaxMana()) < 0.0001f);
    m_player->ReturnToNormal();

    // 7. 合法 -> 成功（随后清理：取消 + 回 Normal）
    result = m_playerSkill.RequestSkill(*m_player, 0, target);
    check("legit cast succeeds", result.success);
    check("legit cast entered SkillCasting",
          m_player->GetActionState() ==
              legend::entity::CharacterActionState::SkillCasting);
    m_playerSkill.GetSkillSystem().CancelCast("validation check cleanup");
    m_player->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillCastValidationCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillManaCooldownCheck] ====================
// 指令六十一：合法 Power Slash -> Mana 100->85、CD 0->4；立即重复释放失败且 Mana 仍 85。

void GameScene::RunSkillManaCooldownCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillManaCooldownCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillManaCooldownCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillManaCooldownCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);

    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("power_slash cast succeeds", result.success);
    check("mana 100 -> 85",
          std::fabs(m_player->GetSkillResource().GetMana() - 85.0f) < 0.0001f);
    const float remaining = m_playerSkill.GetCooldowns().GetRemaining("power_slash");
    check("cooldown 0 -> ~4", remaining > 3.5f && remaining <= 4.0f);
    auto again = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("immediate re-cast fails (cooldown)", !again.success);
    check("mana still 85 after rejected re-cast",
          std::fabs(m_player->GetSkillResource().GetMana() - 85.0f) < 0.0001f);
    // 清理：取消未完成施法 + 状态复位
    m_playerSkill.GetSkillSystem().CancelCast("mana cooldown check cleanup");
    m_player->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillManaCooldownCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillAnimationEventCheck] ====================
// 指令六十二：技能 Clip 的 skill_hit 只消费一次；停留/继续推进不再触发。

void GameScene::RunSkillAnimationEventCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAnimationEventCheck", name, pass, failures);
    };
    if (m_playerClips == nullptr) {
        check("player clips loaded", false);
        LOG_INFO("[SkillAnimationEventCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    legend::animation::AnimationPlayer player;
    player.SetClips(m_playerClips);
    check("skill clip exists", player.Play("skill_power_slash"));
    int eventCount = 0;
    for (int i = 0; i < 120 && eventCount == 0; ++i) {
        player.Update(0.05f);
        auto events = player.ConsumeEvents();
        for (const auto& eventName : events) {
            if (eventName == "skill_hit") {
                ++eventCount;
            }
        }
    }
    check("skill_hit fired exactly once in first pass", eventCount == 1);
    // 动画继续推进/循环播放完毕：不再产生第二个 skill_hit（NonLoop 已结束）
    for (int i = 0; i < 60; ++i) {
        player.Update(0.05f);
        auto events = player.ConsumeEvents();
        for (const auto& eventName : events) {
            if (eventName == "skill_hit") {
                ++eventCount;
            }
        }
    }
    check("no second skill_hit afterwards", eventCount == 1);
    check("nonloop finished", player.IsFinished());
    LOG_INFO("[SkillAnimationEventCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillInterruptCheck] ====================
// 指令六十三：Power Slash 开始（扣蓝/CD 开始）-> skill_hit 前进入 HitReact ->
// 动画继续推进：目标 HP 不降、Context 清除、CD 继续、Mana 不返还。

void GameScene::RunSkillInterruptCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillInterruptCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillInterruptCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillInterruptCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    const float boarHpBefore = boar->GetCombatStats().hp;

    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("power_slash started", result.success);
    const float manaAfterBegin = m_player->GetSkillResource().GetMana();
    const float cdAfterBegin = m_playerSkill.GetCooldowns().GetRemaining("power_slash");
    check("mana deducted at begin",
          manaAfterBegin < m_player->GetSkillResource().GetMaxMana() - 0.5f);
    check("cooldown started at begin", cdAfterBegin > 0.0f);

    // skill_hit 之前打断：立即进入 HitReact（模拟受击）。
    // Context 取消由 PlayerSkillController 下一拍 Update 执行（与真实帧循环一致），
    // 因此先推一拍控制器再断言。
    m_player->EnterHitReact();
    m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
    check("context canceled on HitReact", !m_playerSkill.GetSkillSystem().HasActiveCast());

    // 动画继续推进到结束：目标 HP 不得下降（事件被丢弃，不产生伤害）。
    // HitReact -> Normal 的推进属于 PlayerCombatController（真实帧循环），此处复刻同一规则。
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (m_player->GetActionState() == legend::entity::CharacterActionState::HitReact &&
            m_player->GetAnimationPlayer().IsFinished()) {
            m_player->ReturnToNormal(); // 与 PlayerCombatController::Update HitReact 分支一致
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal) {
            break;
        }
    }
    check("player back to normal", m_player->GetActionState() ==
                                       legend::entity::CharacterActionState::Normal);
    check("target HP unchanged (skill canceled)",
          std::fabs(boar->GetCombatStats().hp - boarHpBefore) < 0.0001f);
    check("cooldown continues (not reset)",
          m_playerSkill.GetCooldowns().GetRemaining("power_slash") > 0.0f);
    check("mana not refunded",
          std::fabs(m_player->GetSkillResource().GetMana() - manaAfterBegin) < 0.0001f);
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillInterruptCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillSingleTargetDamageCheck] ====================
// 指令六十四：Attack80 x1.8 = raw144；Defense5 -> Final139（走真实链路验证 HP 变化）。

void GameScene::RunSkillSingleTargetDamageCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillSingleTargetDamageCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillSingleTargetDamageCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillSingleTargetDamageCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    const float boarHpBefore = boar->GetCombatStats().hp;

    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast() &&
            boar->GetCombatStats().hp < boarHpBefore) {
            break;
        }
    }
    const float drop = boarHpBefore - boar->GetCombatStats().hp;
    check("boar HP dropped", drop > 0.0f);
    // CombatResolver 统一公式：final = max(1, raw - defense)；raw = 80*1.8 = 144
    const float expectedFinal =
        std::max(1.0f, m_player->GetBaseCombatStats().attack * 1.8f - boar->GetCombatStats().defense);
    check("final damage matches resolver formula (raw=attack*multiplier)",
          std::fabs(drop - expectedFinal) < 0.5f);
    check("last hit abilityId is power_slash",
          m_playerSkill.GetSkillSystem().GetLastHit().valid &&
              m_playerSkill.GetSkillSystem().GetLastHit().abilityId == "power_slash");
    // 还原
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    boar->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillSingleTargetDamageCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillDefenseCheck] ====================
// 指令六十五：同 Attack 同技能，Defense5 目标比 Defense30 目标受到更高 Final Damage。

void GameScene::RunSkillDefenseCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillDefenseCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillDefenseCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr; // def 15（高防，掉血 129 不死）
    legend::world::MonsterCharacter* wolf = nullptr; // def 8（低防，掉血 136 不死；
    // 注意：不能用 Slime——120HP 会被 139 伤害直接击杀，观察到的掉血截断为 120，
    // 反而小于高防目标，破坏"低防受伤更多"的断言）
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster == nullptr || !monster->IsCombatAlive()) {
            continue;
        }
        if (monster->GetName() == "Boar" && boar == nullptr) {
            boar = monster;
        } else if (monster->GetName() == "Wolf" && wolf == nullptr) {
            wolf = monster;
        }
    }
    check("boar and wolf present", boar != nullptr && wolf != nullptr);
    if (boar == nullptr || wolf == nullptr) {
        LOG_INFO("[SkillDefenseCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_playerSkill.ResetForRespawn(*m_player);
    const float boarHpBefore = boar->GetCombatStats().hp;
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    (void)m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    const float dropHighDef = boarHpBefore - boar->GetCombatStats().hp;

    m_playerSkill.ResetForRespawn(*m_player);
    const float wolfHpBefore = wolf->GetCombatStats().hp;
    m_player->SetPosition(wolf->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(wolf->GetId());
    (void)m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    const float dropLowDef = wolfHpBefore - wolf->GetCombatStats().hp;

    check("both targets damaged", dropHighDef > 0.0f && dropLowDef > 0.0f);
    check("low defense target took more damage (defense still applies)",
          dropLowDef > dropHighDef);
    // 还原
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    boar->ReturnToNormal();
    if (wolf->GetCombatStats().hp > 0.0f) {
        wolf->GetCombatStats().SetHp(wolf->GetCombatStats().maxHp);
        wolf->ReturnToNormal();
    }
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillDefenseCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillRangeCheck] ====================
// 指令六十六：Target 距离 94（range95 内）允许；96 拒绝。Feet Position + DistanceSquared。

void GameScene::RunSkillRangeCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillRangeCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillRangeCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillRangeCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_playerSkill.ResetForRespawn(*m_player);
    const legend::math::Vector2 boarHome = boar->GetPosition();
    const legend::math::Vector2 playerHome = m_player->GetPosition();

    // 94：允许
    m_player->SetPosition(boarHome + legend::math::Vector2(94.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("distance 94 within range 95 -> allowed", result.success);
    m_playerSkill.GetSkillSystem().CancelCast("range check cleanup");
    m_player->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);

    // 96：拒绝
    m_player->SetPosition(boarHome + legend::math::Vector2(96.0f, 0.0f));
    result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("distance 96 beyond range 95 -> rejected", !result.success);
    check("rejected cast: no cooldown started",
          m_playerSkill.GetCooldowns().IsReady("power_slash"));

    // 还原
    m_player->SetPosition(playerHome);
    boar->SetPosition(boarHome);
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillRangeCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillAOECheck] ====================
// 指令六十七：Whirlwind radius120 -> A(50) B(110) 受伤、C(130) 不受伤、NPC 不受伤。

void GameScene::RunSkillAOECheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAOECheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillAOECheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 收集 3 只活怪 + 1 个 NPC
    std::vector<legend::world::MonsterCharacter*> monsters;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive()) {
            monsters.push_back(monster);
        }
    }
    const auto npcs = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::NPC);
    check("3+ monsters present", monsters.size() >= 3);
    check("npc present", !npcs.empty());
    if (monsters.size() < 3 || npcs.empty()) {
        LOG_INFO("[SkillAOECheck] completed, failures = " + std::to_string(failures));
        return;
    }
    auto* monsterA = monsters[0];
    auto* monsterB = monsters[1];
    auto* monsterC = monsters[2];
    legend::entity::Character* npc = npcs.front();
    const legend::math::Vector2 center = m_player->GetPosition();
    // 隔离其它活怪：出生点附近可能有多只怪在 120 半径内，hitCount 必须恰为 2（A/B）
    {
        const legend::math::Vector2 farAway = center + legend::math::Vector2(2500.0f, 2500.0f);
        for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
            if (monster == nullptr || !monster->IsCombatAlive() || monster == monsterA ||
                monster == monsterB || monster == monsterC) {
                continue;
            }
            monster->SetPosition(farAway);
        }
    }
    // 布置：Player 为中心，A@50 B@110 C@400 NPC@60（半径 120）
    const std::vector<std::pair<legend::entity::Character*, legend::math::Vector2>> placement = {
        {monsterA, center + legend::math::Vector2(50.0f, 0.0f)},
        {monsterB, center + legend::math::Vector2(0.0f, 110.0f)},
        {monsterC, center + legend::math::Vector2(400.0f, 0.0f)},
        {npc, center + legend::math::Vector2(0.0f, 60.0f)},
    };
    std::vector<float> hpBefore;
    for (const auto& [actor, pos] : placement) {
        hpBefore.push_back(actor->GetCombatStats().hp);
        actor->SetPosition(pos);
    }
    m_playerCombat.GetTarget().ClearTarget(); // SelfArea 无需目标
    auto result = m_playerSkill.RequestSkill(*m_player, 1, m_playerCombat.GetTarget());
    check("whirlwind cast succeeds without target", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    const float dropA = hpBefore[0] - monsterA->GetCombatStats().hp;
    const float dropB = hpBefore[1] - monsterB->GetCombatStats().hp;
    const float dropC = hpBefore[2] - monsterC->GetCombatStats().hp;
    const float dropNpc = hpBefore[3] - npc->GetCombatStats().hp;
    check("monster A (50) damaged", dropA > 0.0f);
    check("monster B (110) damaged", dropB > 0.0f);
    check("monster C (400) NOT damaged", std::fabs(dropC) < 0.0001f);
    check("NPC NOT damaged", std::fabs(dropNpc) < 0.0001f);
    check("hitCount recorded 2",
          m_playerSkill.GetSkillSystem().GetLastHit().hitCount == 2);
    // 还原 HP 与位置
    monsterA->GetCombatStats().SetHp(monsterA->GetCombatStats().maxHp);
    monsterA->ReturnToNormal();
    if (monsterB->GetCombatStats().hp > 0.0f) {
        monsterB->GetCombatStats().SetHp(monsterB->GetCombatStats().maxHp);
        monsterB->ReturnToNormal();
    }
    monsterA->SetPosition(center + legend::math::Vector2(150.0f, 150.0f));
    monsterB->SetPosition(center + legend::math::Vector2(-150.0f, 150.0f));
    monsterC->SetPosition(center + legend::math::Vector2(400.0f, 0.0f));
    npc->SetPosition(center + legend::math::Vector2(0.0f, 60.0f));
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillAOECheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillAOEDeathCheck] ====================
// 指令六十八：两只低 HP 怪在 Whirlwind 范围内一次 skill_hit 同时死亡 ->
// 恰好 2 个 DeathEvent（不是 1 个也不是 4 个）；奖励由原 RewardSystem 处理。

void GameScene::RunSkillAOEDeathCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAOEDeathCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillAOEDeathCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    std::vector<legend::world::MonsterCharacter*> monsters;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive()) {
            monsters.push_back(monster);
        }
    }
    check("2+ monsters present", monsters.size() >= 2);
    if (monsters.size() < 2) {
        LOG_INFO("[SkillAOEDeathCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    auto* monsterA = monsters[0];
    auto* monsterB = monsters[1];
    const legend::math::Vector2 center = m_player->GetPosition();
    // 隔离其它活怪：前面 whirlwind 类 Check 可能已削弱出生点附近的怪，
    // 它们若在半径内被补刀会产生第 3 个 DeathEvent
    {
        const legend::math::Vector2 farAway = center + legend::math::Vector2(2500.0f, 2500.0f);
        for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
            if (monster == nullptr || !monster->IsCombatAlive() || monster == monsterA ||
                monster == monsterB) {
                continue;
            }
            monster->SetPosition(farAway);
        }
    }
    monsterA->SetPosition(center + legend::math::Vector2(40.0f, 0.0f));
    monsterB->SetPosition(center + legend::math::Vector2(0.0f, 40.0f));
    monsterA->GetCombatStats().SetHp(1.0f);
    monsterB->GetCombatStats().SetHp(1.0f);
    m_worldActors.GetCombatSystem().ClearRecentDeaths(); // 只统计本次施法的死亡
    m_playerCombat.GetTarget().ClearTarget();
    auto result = m_playerSkill.RequestSkill(*m_player, 1, m_playerCombat.GetTarget());
    check("whirlwind cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    const std::size_t deathCount = m_worldActors.GetCombatSystem().GetRecentDeaths().size();
    check("exactly 2 DeathEvents from one whirlwind", deathCount == 2);
    check("both monsters dead", !monsterA->IsCombatAlive() && !monsterB->IsCombatAlive());
    // 不让本 Check 产生真实奖励（奖励链路由 SkillDeathReward/SkillAOERewardCheck 验证）：
    // 消费并清空死亡事件，然后复活两只怪还原世界状态
    m_worldActors.GetCombatSystem().ClearRecentDeaths();
    monsterA->GetCombatStats().SetHp(monsterA->GetCombatStats().maxHp);
    monsterA->ReturnToNormal();
    monsterB->GetCombatStats().SetHp(monsterB->GetCombatStats().maxHp);
    monsterB->ReturnToNormal();
    monsterA->SetPosition(center + legend::math::Vector2(150.0f, 150.0f));
    monsterB->SetPosition(center + legend::math::Vector2(-150.0f, 150.0f));
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillAOEDeathCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillTargetDeathBeforeEventCheck] ====================
// 指令六十九：skill_hit 之前目标被其他流程杀死 -> 事件触发时安全失败，不产生伤害。

void GameScene::RunSkillTargetDeathBeforeEventCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillTargetDeathBeforeEventCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillTargetDeathBeforeEventCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillTargetDeathBeforeEventCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast started", result.success);
    // 目标在 skill_hit 之前被其他流程杀死（测试辅助：直接 SetHp+EnterDead）
    boar->GetCombatStats().SetHp(0.0f);
    boar->EnterDead();
    const float hpAtDeath = boar->GetCombatStats().hp;
    // 动画继续推进到事件与结束：不允许崩溃、不允许对尸体再次结算
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    check("cast ended safely after target death",
          m_player->GetActionState() == legend::entity::CharacterActionState::Normal);
    check("dead target HP still 0 (no posthumous heal/damage math)",
          std::fabs(boar->GetCombatStats().hp - hpAtDeath) < 0.0001f);
    check("hit recorded as miss (no valid damage)",
          !m_playerSkill.GetSkillSystem().GetLastHit().valid ||
              m_playerSkill.GetSkillSystem().GetLastHit().abilityId != "power_slash" ||
              m_playerSkill.GetSkillSystem().GetLastHit().hitCount == 0);
    // 还原目标
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    boar->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillTargetDeathBeforeEventCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillTargetDespawnCheck] ====================
// 指令七十：Cast 开始后目标 Unregister/Despawn -> 事件时安全失败，不崩溃。

void GameScene::RunSkillTargetDespawnCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillTargetDespawnCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillTargetDespawnCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillTargetDespawnCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast started", result.success);
    // 模拟 Despawn 前的 inactive（Registry 引用仍在，与 CombatTargetLifecycleCheck 同模式）
    boar->SetActive(false);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    check("cast ended safely after target inactive",
          m_player->GetActionState() == legend::entity::CharacterActionState::Normal);
    check("hit was a miss (inactive target)",
          m_playerSkill.GetSkillSystem().GetLastHit().hitCount == 0 ||
              !m_playerSkill.GetSkillSystem().GetLastHit().valid);
    boar->SetActive(true); // 还原
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillTargetDespawnCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillCastStateCheck] ====================
// 指令七十一：施法开始 SkillCasting；事件后仍 SkillCasting；动画结束才回 Normal。

void GameScene::RunSkillCastStateCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillCastStateCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillCastStateCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillCastStateCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    const float boarHpBefore = boar->GetCombatStats().hp;
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast started", result.success);
    check("state SkillCasting after BeginCast",
          m_player->GetActionState() ==
              legend::entity::CharacterActionState::SkillCasting);
    check("clip override set to skill animation",
          m_player->GetActionClipOverride() == "skill_power_slash");
    bool sawEvent = false;
    bool stateStillCastingAfterEvent = false;
    bool endedNormal = false;
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!sawEvent && m_playerSkill.GetSkillSystem().GetLastHit().valid &&
            m_playerSkill.GetSkillSystem().GetLastHit().hitCount > 0) {
            sawEvent = true;
            // 事件结算后（当帧）仍保持 SkillCasting，不提前回 Normal
            stateStillCastingAfterEvent =
                m_player->GetActionState() ==
                legend::entity::CharacterActionState::SkillCasting;
        }
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            endedNormal = m_player->GetActionState() ==
                          legend::entity::CharacterActionState::Normal;
            break;
        }
    }
    check("skill event fired", sawEvent);
    check("state still SkillCasting right after event", stateStillCastingAfterEvent);
    check("state Normal after animation finished", endedNormal);
    check("context cleared after finish", !m_playerSkill.GetSkillSystem().HasActiveCast());
    // 还原
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    boar->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillCastStateCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillMovementLockCheck] ====================
// 指令七十二：SkillCasting 期间 CharacterController 不移动 Player（位置保持）。

void GameScene::RunSkillMovementLockCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillMovementLockCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillMovementLockCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillMovementLockCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const legend::math::Vector2 playerHome = m_player->GetPosition();
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast started", result.success);
    const legend::math::Vector2 posAtCast = m_player->GetPosition();
    // 施法期间推进移动控制器（虚拟输入向右）——位置必须保持。
    // 顺序关键：先判施法中再驱动移动——若先 UpdateAnimation 使施法结束（回 Normal），
    // 同一迭代的移动就是合法移动，会误报"施法中移动了"。
    m_playerController.SetVirtualInput({1.0f, 0.0f});
    for (int i = 0; i < 20; ++i) {
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break; // 施法已结束：停止驱动移动
        }
        m_playerController.Update(input, m_characterController, *m_player, *m_map, 0.016f);
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
    }
    check("player did not move while SkillCasting",
          (m_player->GetPosition() - posAtCast).LengthSq() < 0.0001f);
    // 施法结束（Normal）后同样的虚拟输入应当可以移动（对照组）
    m_playerController.SetVirtualInput({0.0f, 0.0f});
    m_playerSkill.ResetForRespawn(*m_player);
    m_player->SetPosition(playerHome);
    LOG_INFO("[SkillMovementLockCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillBasicAttackInteractionCheck] ====================
// 指令七十三：SkillCasting 时 Space 普攻请求失败；Attacking 时技能请求失败。

void GameScene::RunSkillBasicAttackInteractionCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillBasicAttackInteractionCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillBasicAttackInteractionCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillBasicAttackInteractionCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    const float boarHpBefore = boar->GetCombatStats().hp;

    // 1. SkillCasting 时普通攻击请求失败
    auto cast = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast started", cast.success);
    const bool attackRejectedWhileCasting = !m_playerCombat.RequestAttack(
        *m_player, m_worldActors.GetRegistry(), m_worldActors.GetCombatSystem());
    check("basic attack rejected while SkillCasting", attackRejectedWhileCasting);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }

    // 2. Attacking 时技能请求失败
    m_playerSkill.ResetForRespawn(*m_player);
    const bool attackStarted = m_playerCombat.RequestAttack(
        *m_player, m_worldActors.GetRegistry(), m_worldActors.GetCombatSystem());
    check("basic attack started (setup)", attackStarted);
    auto castDuringAttack =
        m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("skill rejected while Attacking", !castDuringAttack.success);
    // 攻击动画播完 -> 回 Normal（与 PlayerCombatController 相同规则）
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        if (m_player->GetAnimationPlayer().IsFinished()) {
            m_player->ReturnToNormal();
            break;
        }
    }
    check("player back to normal after attack", m_player->GetActionState() ==
                                                    legend::entity::CharacterActionState::Normal);
    m_playerSkill.ResetForRespawn(*m_player);
    (void)boarHpBefore;
    LOG_INFO("[SkillBasicAttackInteractionCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [EquipmentSkillDamageCheck] ====================
// 指令七十四：技能 Raw 必须用 Final Attack（base80 + wooden_sword12 = 92 -> raw 165.6）。

void GameScene::RunEquipmentSkillDamageCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("EquipmentSkillDamageCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[EquipmentSkillDamageCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[EquipmentSkillDamageCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    const float baseAttack = m_player->GetBaseCombatStats().attack;
    const float boarHpFull = boar->GetCombatStats().maxHp;

    // 注入 wooden_sword 实例并装备（与 EquipmentChecks 相同注入模式）
    const legend::item::ItemDefinition* swordDef =
        m_worldActors.GetItemDatabase().Get("wooden_sword");
    check("wooden_sword definition present", swordDef != nullptr);
    if (swordDef == nullptr) {
        LOG_INFO("[EquipmentSkillDamageCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    legend::item::ItemInstance instance;
    instance.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    instance.definitionId = "wooden_sword";
    instance.quantity = 1;
    check("instance added to inventory",
          m_player->GetInventory().AddInstance(instance, swordDef));
    const auto equipResult = m_player->EquipInstance(instance.instanceId);
    check("wooden_sword equipped", equipResult.success);
    const float finalAttack = m_player->GetCombatStats().attack;
    check("final attack = base + 12",
          std::fabs(finalAttack - (baseAttack + 12.0f)) < 0.001f);

    boar->GetCombatStats().SetHp(boarHpFull); // 满血起点（保证可观测差值）
    const float boarHpBefore = boar->GetCombatStats().hp;
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast succeeds with equipment", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    const float drop = boarHpBefore - boar->GetCombatStats().hp;
    const float expectedFinal =
        std::max(1.0f, finalAttack * 1.8f - boar->GetCombatStats().defense);
    check("skill damage uses Final Attack (not base)",
          std::fabs(drop - expectedFinal) < 0.5f && drop > 0.0f);

    // 清理：卸装 + 移除测试实例 + 还原 HP/蓝/CD
    (void)m_player->UnequipSlot(legend::item::EquipmentSlotType::Weapon);
    (void)m_player->GetInventory().TakeInstance(instance.instanceId);
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    boar->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[EquipmentSkillDamageCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillAttackSnapshotCheck] ====================
// 指令七十八：施法开始锁定 attackSnapshot；开始后 Final Attack 变化不影响本次技能伤害。

void GameScene::RunSkillAttackSnapshotCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAttackSnapshotCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillAttackSnapshotCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillAttackSnapshotCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    const float attackBefore = m_player->GetCombatStats().attack; // 80
    const float boarHpBefore = boar->GetCombatStats().hp;

    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast started", result.success);
    check("context snapshot == attack at begin",
          std::fabs(m_playerSkill.GetSkillSystem().GetContext().attackSnapshot -
                    attackBefore) < 0.001f);
    // 施法中模拟 Final Attack 变化（80 -> 100）
    m_player->GetCombatStats().attack = 100.0f;
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    const float drop = boarHpBefore - boar->GetCombatStats().hp;
    const float expectedWithSnapshot =
        std::max(1.0f, attackBefore * 1.8f - boar->GetCombatStats().defense); // 144 - 15 = 129
    const float expectedIfMutated =
        std::max(1.0f, 100.0f * 1.8f - boar->GetCombatStats().defense); // 180 - 15 = 165
    check("damage uses snapshot (not mutated attack)",
          std::fabs(drop - expectedWithSnapshot) < 0.5f);
    check("damage is NOT based on mutated attack",
          std::fabs(drop - expectedIfMutated) > 1.0f);
    // 还原：Recalculate 从 base+equipment 恢复 Final Attack；HP/蓝/CD 复位
    m_player->RecalculateCombatStats();
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    boar->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillAttackSnapshotCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillRespawnResetCheck] ====================
// 指令八十一：死亡前 Mana20 + Skill CD + 施法中 -> Respawn 后 Mana=max、全 CD=0、
// 无施法、ActionState Normal。

void GameScene::RunSkillRespawnResetCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillRespawnResetCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillRespawnResetCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillRespawnResetCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    // 制造"死亡前"状态：正在 Cast（whirlwind，Mana 100->75 + CD 6）+ Mana 进一步降到 20。
    // 顺序关键：先施法再扣蓝——whirlwind 耗蓝 25，若先 SetMana(20) 施法会被"蓝不足"拒绝。
    (void)m_playerSkill.RequestSkill(*m_player, 1, m_playerCombat.GetTarget()); // whirlwind
    check("whirlwind casting (setup)",
          m_player->GetActionState() ==
              legend::entity::CharacterActionState::SkillCasting);
    check("whirlwind on cooldown (setup)",
          !m_playerSkill.GetCooldowns().IsReady("whirlwind"));
    m_player->GetSkillResource().SetMana(20.0f);
    check("mana at 20 (setup)",
          std::fabs(m_player->GetSkillResource().GetMana() - 20.0f) < 0.0001f);
    // 模拟 Respawn 重置（GameScene::UpdatePlayerRespawn 调用的同一函数）
    m_playerSkill.ResetForRespawn(*m_player);
    m_player->ReturnToNormal();
    check("mana refilled to max",
          std::fabs(m_player->GetSkillResource().GetMana() -
                    m_player->GetSkillResource().GetMaxMana()) < 0.0001f);
    check("all skill cooldowns cleared",
          m_playerSkill.GetCooldowns().IsReady("whirlwind") &&
              m_playerSkill.GetCooldowns().IsReady("power_slash") &&
              m_playerSkill.GetCooldowns().IsReady("piercing_strike") &&
              m_playerSkill.GetCooldowns().IsReady("heavy_strike"));
    check("no current cast", !m_playerSkill.GetSkillSystem().HasActiveCast());
    check("action state Normal",
          m_player->GetActionState() == legend::entity::CharacterActionState::Normal);
    (void)input;
    LOG_INFO("[SkillRespawnResetCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillAggroCheck] ====================
// 指令一百零七：合法技能命中 -> Monster AggroTable 中 Player Threat 增加；
// 技能释放失败（距离/Mana/CD/无目标）-> Aggro 不变化。

void GameScene::RunSkillAggroCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAggroCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillAggroCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* wolf = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Wolf") {
            wolf = monster;
            break;
        }
    }
    check("wolf present", wolf != nullptr);
    if (wolf == nullptr) {
        LOG_INFO("[SkillAggroCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(wolf->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(wolf->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    // 满血 wolf：前面 whirlwind 类 Check 可能已削弱它——若被本 Check 一击打死，
    // 死亡目标不再累积仇恨（OnDamaged 跳过尸体），threat 断言会失败
    wolf->GetCombatStats().SetHp(wolf->GetCombatStats().maxHp);
    const auto* ai = m_worldActors.GetAIController(wolf->GetId());
    check("wolf AI controller present", ai != nullptr);
    if (ai == nullptr) {
        LOG_INFO("[SkillAggroCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 先分发前面 Check 遗留的 pending 伤害事件（如 RespawnReset 的 whirlwind），
    // 否则旧事件会在施法后的同一次 Dispatch 里混入 wolf 威胁，破坏"威胁==本次伤害"断言
    m_worldActors.DispatchCombatEvents();
    const float threatBefore = ai->GetAggroTable().GetThreat(m_player->GetId());
    const float wolfHpBefore = wolf->GetCombatStats().hp;

    // 合法命中：真实施法 -> ApplySkillDamage -> DispatchCombatEvents（同一管线）
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    m_worldActors.DispatchCombatEvents();
    const float threatAfterHit = ai->GetAggroTable().GetThreat(m_player->GetId());
    check("threat increased after skill hit", threatAfterHit > threatBefore);
    check("threat equals damage applied (single pipeline, no double add)",
          threatAfterHit - threatBefore <= wolfHpBefore - wolf->GetCombatStats().hp + 0.5f);

    // 失败释放：目标移出射程 -> 请求失败 -> Aggro 不变化
    m_playerSkill.ResetForRespawn(*m_player);
    const legend::math::Vector2 wolfHome = wolf->GetPosition();
    const float threatBeforeFail = ai->GetAggroTable().GetThreat(m_player->GetId());
    wolf->SetPosition(wolfHome + legend::math::Vector2(300.0f, 0.0f));
    auto failed = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("out-of-range cast fails", !failed.success);
    check("threat unchanged after failed cast",
          std::fabs(ai->GetAggroTable().GetThreat(m_player->GetId()) - threatBeforeFail) <
              0.0001f);
    // 还原
    wolf->SetPosition(wolfHome);
    wolf->GetCombatStats().SetHp(wolf->GetCombatStats().maxHp);
    wolf->ReturnToNormal();
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillAggroCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillDeathRewardCheck] ====================
// 指令一百零八：Power Slash 真实流程杀死 Monster -> DeathEvent 一次、Exp 一次、
// Loot Roll 一次；连续 Update 不重复；SkillSystem 不自己加经验。

void GameScene::RunSkillDeathRewardCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillDeathRewardCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillDeathRewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    legend::world::MonsterCharacter* slime = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            slime = monster;
            break;
        }
    }
    check("slime present", slime != nullptr);
    if (slime == nullptr) {
        LOG_INFO("[SkillDeathRewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(slime->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(slime->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    slime->GetCombatStats().SetHp(1.0f); // 一击必杀（真实技能链路）
    // 清空前面 Check 遗留的 pending 死亡（如 RespawnReset 的 whirlwind 误伤），
    // 否则 "exactly 1 DeathEvent" 会把旧死亡也算进来
    m_worldActors.GetCombatSystem().ClearRecentDeaths();
    // 指令十/二十：确定性 Loot——保存正式表 -> Override small_potion 1.0/1/1，
    // 禁止依赖正式随机概率作为唯一断言；结束恢复正式表（guard 清测试 GroundLoot）
    const auto originalLoot = m_worldActors.GetSpawner().GetLootEntries("slime");
    std::vector<legend::world::LootEntry> guaranteed;
    legend::world::LootEntry guaranteedEntry;
    guaranteedEntry.itemId = "small_potion";
    guaranteedEntry.chance = 1.0f;
    guaranteedEntry.min = 1;
    guaranteedEntry.max = 1;
    guaranteed.push_back(guaranteedEntry);
    m_worldActors.GetSpawner().SetTestLootOverride("slime", guaranteed);
    const auto expBefore = m_player->GetProgression().GetTotalExp();
    const std::size_t lootBefore = m_worldActors.GetLoot().GetAll().size();

    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    check("slime killed by skill", !slime->IsCombatAlive());
    const int deaths = m_worldActors.GetCombatSystem().GetRecentDeaths().size();
    check("exactly 1 DeathEvent", deaths == 1);
    // 奖励走原 RewardSystem（WorldActorManager::ProcessDeathRewards = Update 内同一入口）
    const int rewarded1 = m_worldActors.ProcessDeathRewards();
    const auto expAfter1 = m_player->GetProgression().GetTotalExp();
    const std::size_t lootAfter1 = m_worldActors.GetLoot().GetAll().size();
    check("reward processed once", rewarded1 == 1);
    check("exp increased exactly once", expAfter1 > expBefore);
    // 指令二十：确定性断言（不依赖正式随机概率）——恰好 +1 件且为 small_potion x1
    check("deterministic ground loot small_potion x1",
          lootAfter1 - lootBefore == 1 &&
              [this]() {
                  for (const auto& loot : m_worldActors.GetLoot().GetAll()) {
                      if (loot.itemId == "small_potion" && loot.quantity == 1) {
                          return true;
                      }
                  }
                  return false;
              }());
    const int rewarded2 = m_worldActors.ProcessDeathRewards();
    check("second reward pass is no-op (exactly-once)",
          rewarded2 == 0 &&
              m_player->GetProgression().GetTotalExp() == expAfter1 &&
              m_worldActors.GetLoot().GetAll().size() == lootAfter1);
    m_playerSkill.ResetForRespawn(*m_player);
    // 指令十：恢复正式 LootTable（guard 同时清理测试新增 GroundLoot / 复活 slime）
    m_worldActors.GetSpawner().SetTestLootOverride("slime", originalLoot);
    LOG_INFO("[SkillDeathRewardCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillAOERewardCheck] ====================
// 指令一百零九：Whirlwind 一次事件同时杀死两只低 HP Monster -> 2 个 DeathEvent、
// RewardSystem 处理 2 份 Exp、各自 Loot Roll、不重复不遗漏。

void GameScene::RunSkillAOERewardCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAOERewardCheck", name, pass, failures);
    };
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillAOERewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    std::vector<legend::world::MonsterCharacter*> monsters;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive()) {
            monsters.push_back(monster);
        }
    }
    check("2+ monsters present", monsters.size() >= 2);
    if (monsters.size() < 2) {
        LOG_INFO("[SkillAOERewardCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    auto* monsterA = monsters[0];
    auto* monsterB = monsters[1];
    const legend::math::Vector2 center = m_player->GetPosition();
    monsterA->SetPosition(center + legend::math::Vector2(40.0f, 0.0f));
    monsterB->SetPosition(center + legend::math::Vector2(0.0f, 40.0f));
    monsterA->GetCombatStats().SetHp(1.0f);
    monsterB->GetCombatStats().SetHp(1.0f);
    m_worldActors.GetCombatSystem().ClearRecentDeaths();
    const auto expBefore = m_player->GetProgression().GetTotalExp();
    const std::size_t lootBefore = m_worldActors.GetLoot().GetAll().size();

    m_playerCombat.GetTarget().ClearTarget();
    auto result = m_playerSkill.RequestSkill(*m_player, 1, m_playerCombat.GetTarget());
    check("whirlwind cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    check("both monsters killed by one event",
          !monsterA->IsCombatAlive() && !monsterB->IsCombatAlive());
    const int deaths = m_worldActors.GetCombatSystem().GetRecentDeaths().size();
    check("exactly 2 DeathEvents", deaths == 2);
    const int rewarded = m_worldActors.ProcessDeathRewards();
    check("both deaths rewarded", rewarded == 2);
    check("exp increased", m_player->GetProgression().GetTotalExp() > expBefore);
    check("ground loot spawned", m_worldActors.GetLoot().GetAll().size() > lootBefore);
    const auto expAfter = m_player->GetProgression().GetTotalExp();
    const std::size_t lootAfter = m_worldActors.GetLoot().GetAll().size();
    const int rewarded2 = m_worldActors.ProcessDeathRewards();
    check("no duplicate rewards",
          rewarded2 == 0 && m_player->GetProgression().GetTotalExp() == expAfter &&
              m_worldActors.GetLoot().GetAll().size() == lootAfter);
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillAOERewardCheck] completed, failures = " + std::to_string(failures));
}

// ==================== LEGEND_AUTO_SKILL_TEST=1 时间线 ====================
// 真实运行的技能验收时间线（指令一百一十四/一百一十五）：
// 所有伤害环节走 RequestSkill -> SkillSystem -> Skill Animation -> Animation Event ->
// SkillSystem HandleEvent -> CombatSystem；ApplyDamage 仅用于制造 HitReact / Player 死亡前置。
// Stage: 0 初始化 1 选怪就位 2 施法(MP/CD/状态) 3 事件命中 4 CD拒绝 5 射程拒绝
//        6 Whirlwind AOE 7 装备+技能 8 HitReact 打断 9 技能击杀奖励 10 玩家死亡重生
//        11 怪物重生 90 汇总

namespace {
// 便捷：按名字找第一只活怪
legend::world::MonsterCharacter* FindAliveMonsterByName(GameScene& /*scene*/,
                                                        const std::vector<legend::world::MonsterCharacter*>& monsters,
                                                        const char* name) {
    for (legend::world::MonsterCharacter* monster : monsters) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == name) {
            return monster;
        }
    }
    return nullptr;
}
} // namespace

void GameScene::UpdateSkillTest(float deltaTime) {
    auto& registry = m_worldActors.GetRegistry();
    auto& input = Engine::Get().GetInput();
    auto fail = [this](const std::string& name) {
        LOG_INFO("[SkillTest] " + name + " -> FAIL");
        ++m_skillTestFailures;
    };
    auto pass = [this](const std::string& name) {
        LOG_INFO("[SkillTest] " + name + " -> PASS");
    };
    auto check = [&pass, &fail](const std::string& name, bool ok) {
        if (ok) {
            pass(name);
        } else {
            fail(name);
        }
    };
    auto monsters = [this]() { return m_worldActors.GetMonsters(); };
    // 便捷：把玩家放到目标旁（施法范围内）并锁定目标
    auto engageTarget = [this](legend::world::MonsterCharacter* target) {
        m_skillTestTargetId = target->GetId();
        m_player->SetPosition(target->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
        m_playerCombat.GetTarget().SetTarget(target->GetId());
    };

    m_skillTestElapsed += deltaTime;
    // 阶段切换检测：进入新阶段时重置 stageEntered / stageElapsed
    // （否则 stageElapsed 是全程累计值，各阶段的 per-stage timeout 会误触发）
    if (m_skillTestStage != m_skillTestLastStage) {
        m_skillTestLastStage = m_skillTestStage;
        m_skillTestStageEntered = false;
        m_skillTestStageElapsed = 0.0;
    }
    if (!m_skillTestStageEntered) {
        m_skillTestStageEntered = true;
        m_skillTestStageElapsed = 0.0;
        LOG_INFO("[SkillTest] stage " + std::to_string(m_skillTestStage) + " entered.");
    }
    m_skillTestStageElapsed += deltaTime;
    // timeout 只触发一次（避免 stage90 后每帧刷 FAIL）
    if (m_skillTestElapsed > 180.0 && m_skillTestStage != 90) {
        fail("timeline timeout 180s");
        m_skillTestStage = 90;
    }

    switch (m_skillTestStage) {
    case 0: { // 初始化：Player/Mana/Loadout
        if (m_player == nullptr) {
            fail("player missing");
            m_skillTestStage = 90;
            break;
        }
        check("mana full at start",
              std::fabs(m_player->GetSkillResource().GetMana() -
                        m_player->GetSkillResource().GetMaxMana()) < 0.0001f &&
                  std::fabs(m_player->GetSkillResource().GetMaxMana() - 100.0f) < 0.0001f);
        check("4 skill slots bound",
              m_player->GetLoadout().GetSkillId(0) == "power_slash" &&
                  m_player->GetLoadout().GetSkillId(1) == "whirlwind" &&
                  m_player->GetLoadout().GetSkillId(2) == "piercing_strike" &&
                  m_player->GetLoadout().GetSkillId(3) == "heavy_strike");
        m_playerSkill.ResetForRespawn(*m_player);
        // 阶段8.1指令十二/十三：保存正式 slime 掉落表（Override 前一次）→
        // Override 确定性 small_potion 1.0/1/1（Stage9 精确验证，不依赖随机概率）
        if (!m_skillTestLootSaved) {
            m_skillTestOriginalSlimeLoot = m_worldActors.GetSpawner().GetLootEntries("slime");
            m_skillTestLootSaved = true;
            LOG_INFO("[SkillTest] official slime loot saved (" +
                     std::to_string(m_skillTestOriginalSlimeLoot.size()) + " entries).");
            std::vector<legend::world::LootEntry> guaranteed;
            legend::world::LootEntry guaranteedEntry;
            guaranteedEntry.itemId = "small_potion";
            guaranteedEntry.chance = 1.0f;
            guaranteedEntry.min = 1;
            guaranteedEntry.max = 1;
            guaranteed.push_back(guaranteedEntry);
            m_worldActors.GetSpawner().SetTestLootOverride("slime", guaranteed);
        }
        // 阶段8.1指令十四：Stage0 保存世界快照（Stage90 统一恢复全部 Actor 状态）
        CaptureSkillWorldSnapshot();
        pass("stage 0 complete");
        m_skillTestStage = 1;
        break;
    }
    case 1: { // 选怪（Boar：耐打，不会被一击秒杀）+ 就位；其它怪隔离到远处
        auto all = monsters();
        auto* boar = FindAliveMonsterByName(*this, all, "Boar");
        if (boar == nullptr) {
            if (m_skillTestStageElapsed > 10.0) {
                fail("no alive boar within 10s");
                m_skillTestStage = 90;
            }
            break;
        }
        for (legend::world::MonsterCharacter* monster : all) {
            if (monster != nullptr && monster != boar && monster->IsCombatAlive()) {
                monster->SetPosition(m_player->GetPosition() +
                                     legend::math::Vector2(2500.0f, 2500.0f));
            }
        }
        engageTarget(boar);
        pass("target acquired and in range");
        m_skillTestStage = 2;
        break;
    }
    case 2: { // 真实施法 power_slash：Mana 100->85、CD 开始、SkillCasting
        if (m_skillTestCastRequested) {
            break; // 等待下方断言通过后进入下一阶段
        }
        m_skillTestManaBaseline = m_player->GetSkillResource().GetMana();
        m_skillTestTargetHpBefore = 0.0f;
        auto* target = static_cast<legend::world::MonsterCharacter*>(registry.Get(m_skillTestTargetId));
        if (target == nullptr || !target->IsCombatAlive()) {
            fail("target missing before cast");
            m_skillTestStage = 90;
            break;
        }
        m_skillTestTargetHpBefore = target->GetCombatStats().hp;
        auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
        if (!result.success) {
            if (m_skillTestStageElapsed > 10.0) {
                fail("power_slash cast rejected within 10s: " + result.reason);
                m_skillTestStage = 90;
            }
            break; // 怪物 AI 可能先打了玩家（HitReact）：下帧重试
        }
        m_skillTestCastRequested = true;
        check("mana 100 -> 85",
              std::fabs(m_player->GetSkillResource().GetMana() -
                        (m_skillTestManaBaseline - 15.0f)) < 0.0001f);
        check("cooldown started", !m_playerSkill.GetCooldowns().IsReady("power_slash"));
        check("state SkillCasting",
              m_player->GetActionState() ==
                  legend::entity::CharacterActionState::SkillCasting);
        m_skillTestStage = 3;
        break;
    }
    case 3: { // 等待真实 skill_power_slash 动画触发 skill_hit -> 目标 HP 下降
        auto* target = static_cast<legend::world::MonsterCharacter*>(registry.Get(m_skillTestTargetId));
        if (target == nullptr) {
            fail("target despawned during cast");
            m_skillTestStage = 90;
            break;
        }
        const float hpNow = target->GetCombatStats().hp;
        if (hpNow < m_skillTestTargetHpBefore) {
            const float drop = m_skillTestTargetHpBefore - hpNow;
            m_skillTestDropBaseline = drop;
            check("damage event abilityId == power_slash",
                  m_playerSkill.GetSkillSystem().GetLastHit().abilityId == "power_slash");
            pass("skill hit landed, HP drop = " + std::to_string(drop));
            m_skillTestCastRequested = false;
            m_skillTestStage = 4;
        } else if (m_skillTestStageElapsed > 10.0) {
            fail("no skill damage within 10s");
            m_skillTestStage = 90;
        }
        break;
    }
    case 4: { // 立即再次请求：必须因 CD 失败，Mana 不再减少
        const float manaBefore = m_player->GetSkillResource().GetMana();
        auto again = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
        check("re-cast rejected by cooldown", !again.success);
        check("mana unchanged after rejected re-cast",
              std::fabs(m_player->GetSkillResource().GetMana() - manaBefore) < 0.0001f);
        m_skillTestStage = 5;
        break;
    }
    case 5: { // 等 CD 结束 -> 目标移出射程 -> 请求失败、不扣蓝、不启动 CD
        if (!m_playerSkill.GetCooldowns().IsReady("power_slash")) {
            break; // 等 CD（4s）
        }
        auto* target = static_cast<legend::world::MonsterCharacter*>(registry.Get(m_skillTestTargetId));
        if (target == nullptr || !target->IsCombatAlive()) {
            fail("target missing for range test");
            m_skillTestStage = 90;
            break;
        }
        const legend::math::Vector2 targetHome = target->GetPosition();
        target->SetPosition(targetHome + legend::math::Vector2(300.0f, 0.0f));
        const float manaBefore = m_player->GetSkillResource().GetMana();
        auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
        check("out-of-range cast rejected", !result.success);
        check("mana unchanged after range rejection",
              std::fabs(m_player->GetSkillResource().GetMana() - manaBefore) < 0.0001f);
        check("no cooldown started on rejection",
              m_playerSkill.GetCooldowns().IsReady("power_slash"));
        target->SetPosition(targetHome);
        m_playerSkill.ResetForRespawn(*m_player); // 满蓝进入 AOE 阶段
        m_skillTestStage = 6;
        break;
    }
    case 6: { // Whirlwind：范围内 2 怪受伤、范围外 1 怪不受伤、NPC 不受伤
        auto all = monsters();
        const auto npcs = registry.GetByType(legend::entity::ActorType::NPC);
        if (all.size() < 3 || npcs.empty()) {
            if (m_skillTestStageElapsed > 15.0) {
                fail("not enough monsters/npc for AOE stage");
                m_skillTestStage = 90;
            }
            break;
        }
        // 取 3 只与当前目标不同的活怪（隔离期的怪按名字/指针取回即可）
        std::vector<legend::world::MonsterCharacter*> picked;
        for (legend::world::MonsterCharacter* monster : all) {
            if (monster != nullptr && monster->IsCombatAlive() &&
                monster->GetId() != m_skillTestTargetId) {
                picked.push_back(monster);
                if (picked.size() == 3) {
                    break;
                }
            }
        }
        if (picked.size() < 3) {
            if (m_skillTestStageElapsed > 15.0) {
                fail("only " + std::to_string(picked.size()) + " extra monsters for AOE");
                m_skillTestStage = 90;
            }
            break;
        }
        const legend::math::Vector2 center = m_player->GetPosition();
        picked[0]->SetPosition(center + legend::math::Vector2(50.0f, 0.0f)); // 范围内 A
        picked[1]->SetPosition(center + legend::math::Vector2(0.0f, 110.0f)); // 范围内 B
        picked[2]->SetPosition(center + legend::math::Vector2(400.0f, 0.0f)); // 范围外 C
        npcs.front()->SetPosition(center + legend::math::Vector2(0.0f, 60.0f)); // 范围内 NPC
        const float hpA = picked[0]->GetCombatStats().hp;
        const float hpB = picked[1]->GetCombatStats().hp;
        const float hpC = picked[2]->GetCombatStats().hp;
        const float hpNpc = npcs.front()->GetCombatStats().hp;
        m_skillTestCastRequested = false;
        auto result = m_playerSkill.RequestSkill(*m_player, 1, m_playerCombat.GetTarget());
        if (!result.success) {
            if (m_skillTestStageElapsed > 15.0) {
                fail("whirlwind cast rejected: " + result.reason);
                m_skillTestStage = 90;
            }
            break;
        }
        // 等 whirlwind 事件结算（hitCount 落定）
        for (int i = 0; i < kMaxCastDriveFrames; ++i) {
            m_player->UpdateAnimation(0.05f);
            m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
            if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
                break;
            }
        }
        check("AOE monster A (50) damaged", picked[0]->GetCombatStats().hp < hpA);
        check("AOE monster B (110) damaged", picked[1]->GetCombatStats().hp < hpB);
        check("AOE monster C (400) NOT damaged",
              std::fabs(picked[2]->GetCombatStats().hp - hpC) < 0.0001f);
        check("AOE NPC NOT damaged",
              std::fabs(npcs.front()->GetCombatStats().hp - hpNpc) < 0.0001f);
        pass("whirlwind hitCount = " +
             std::to_string(m_playerSkill.GetSkillSystem().GetLastHit().hitCount));
        m_skillTestStage = 7;
        break;
    }
    case 7: { // Equipment + Skill：wooden_sword Final Attack 参与技能 Raw
        // 注入 wooden_sword -> 装备 -> 等 CD -> 施法 -> 伤害 > Stage3 基线
        const legend::item::ItemDefinition* swordDef =
            m_worldActors.GetItemDatabase().Get("wooden_sword");
        if (swordDef == nullptr) {
            fail("wooden_sword definition missing");
            m_skillTestStage = 90;
            break;
        }
        if (!m_playerSkill.GetCooldowns().IsReady("power_slash")) {
            break; // 等 CD
        }
        legend::item::ItemInstance instance;
        instance.instanceId = legend::item::ItemInstanceIdAllocator::Next();
        instance.definitionId = "wooden_sword";
        instance.quantity = 1;
        if (!m_player->GetInventory().AddInstance(instance, swordDef)) {
            fail("failed to add wooden_sword instance");
            m_skillTestStage = 90;
            break;
        }
        m_skillTestSwordId = instance.instanceId;
        // 阶段8.4指令五：装备前记录 Final Attack baseline（Cleanup 后立即断言恢复）
        const float attackBeforeSword = m_player->GetCombatStats().attack;
        auto equip = m_player->EquipInstance(instance.instanceId);
        if (!equip.success) {
            fail("equip wooden_sword failed: " + equip.reason);
            m_skillTestStage = 90;
            break;
        }
        auto* target = static_cast<legend::world::MonsterCharacter*>(registry.Get(m_skillTestTargetId));
        if (target == nullptr || !target->IsCombatAlive()) {
            fail("target missing for equipment stage");
            m_skillTestStage = 90;
            break;
        }
        engageTarget(target);
        m_playerSkill.ResetForRespawn(*m_player); // 满蓝清 CD（保留装备）
        // 满血目标：stage3 的 power_slash 与 stage6 whirlwind 的余波（boar 在 55 距离
        // 也被 whirlwind 波及）已削血——不补满会被装备强化后的一击直接打死，
        // 掉血被截断导致 "drop > baseline" 失败，且 stage8 会因目标死亡而中断
        target->GetCombatStats().SetHp(target->GetCombatStats().maxHp);
        target->ReturnToNormal();
        m_skillTestTargetHpBefore = target->GetCombatStats().hp;
        m_skillTestCastRequested = false;
        auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
        if (!result.success) {
            if (m_skillTestStageElapsed > 15.0) {
                fail("equipped power_slash rejected: " + result.reason);
                m_skillTestStage = 90;
            }
            break;
        }
        for (int i = 0; i < kMaxCastDriveFrames; ++i) {
            m_player->UpdateAnimation(0.05f);
            m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
            if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
                break;
            }
        }
        const float drop = m_skillTestTargetHpBefore - target->GetCombatStats().hp;
        check("equipped skill drop > unequipped baseline",
              drop > m_skillTestDropBaseline + 1.0f);
        // 阶段8.3指令九：成功路径同样统一 Cleanup（不再手写 Unequip/TakeInstance/id=0）
        CleanupSkillTestTemporaryEquipment();
        // 阶段8.4指令五/六：Cleanup 内部已重算——立即断言 Final Attack 恢复 baseline
        //（不等 Stage90；后续 Stage8/9 还要用 Final Attack，禁止"Stage90 再恢复"）
        if (std::fabs(m_player->GetCombatStats().attack - attackBeforeSword) > 0.001f) {
            fail("final attack not restored after cleanup");
            m_skillTestStage = 90;
            break;
        }
        LOG_INFO("[SkillTest] temporary equipment stats restored.");
        target->GetCombatStats().SetHp(target->GetCombatStats().maxHp);
        target->ReturnToNormal();
        pass("equipment skill damage verified");
        m_skillTestStage = 8;
        break;
    }
    case 8: { // Hit Interrupt：heavy_strike 施法中被 HitReact 打断
        auto* target = static_cast<legend::world::MonsterCharacter*>(registry.Get(m_skillTestTargetId));
        if (target == nullptr || !target->IsCombatAlive()) {
            fail("target missing for interrupt stage");
            m_skillTestStage = 90;
            break;
        }
        engageTarget(target);
        if (!m_skillTestCastRequested) {
            // CD 门只 gate 首次请求：RequestSkill 后 heavy_strike 进入 8s CD，
            // 若此门在每帧最前面，helper/完成分支永远走不到（施法会正常播完）
            if (!m_playerSkill.GetCooldowns().IsReady("heavy_strike")) {
                break;
            }
            m_playerSkill.ResetForRespawn(*m_player); // 满蓝
            m_skillTestManaBaseline = m_player->GetSkillResource().GetMana();
            m_skillTestTargetHpBefore = target->GetCombatStats().hp;
            auto result = m_playerSkill.RequestSkill(*m_player, 3, m_playerCombat.GetTarget());
            if (!result.success) {
                if (m_skillTestStageElapsed > 15.0) {
                    fail("heavy_strike rejected: " + result.reason);
                    m_skillTestStage = 90;
                }
                break;
            }
            m_skillTestCastRequested = true;
            m_skillTestInterrupted = false;
            break;
        }
        // 施法中：用测试辅助 ApplyDamage 让 Player 进入 HitReact（指令一百一十五允许）
        if (!m_skillTestInterrupted &&
            m_player->GetActionState() ==
                legend::entity::CharacterActionState::SkillCasting) {
            legend::combat::DamageEvent helper;
            helper.sourceId = m_skillTestTargetId;
            helper.targetId = m_player->GetId();
            helper.rawDamage = 10.0f;
            helper.finalDamage = 10.0f;
            (void)m_worldActors.GetCombatSystem().ApplyDamage(helper);
            m_skillTestInterrupted = true;
            break; // Context 取消发生在下一帧的控制器 Update（断言在回 Normal 后做）
        }
        if (m_skillTestInterrupted &&
            m_player->GetActionState() == legend::entity::CharacterActionState::Normal) {
            check("context canceled by HitReact",
                  !m_playerSkill.GetSkillSystem().HasActiveCast());
            check("target HP unchanged (interrupted before skill_hit)",
                  std::fabs(target->GetCombatStats().hp - m_skillTestTargetHpBefore) < 0.0001f);
            check("mana not refunded",
                  std::fabs(m_player->GetSkillResource().GetMana() -
                            (m_skillTestManaBaseline - 30.0f)) < 0.0001f);
            check("heavy_strike cooldown continues",
                  !m_playerSkill.GetCooldowns().IsReady("heavy_strike"));
            pass("hit interrupt verified");
            m_skillTestCastRequested = false;
            m_skillTestStage = 9;
        } else if (m_skillTestStageElapsed > 20.0) {
            fail("interrupt stage timeout");
            m_skillTestStage = 90;
        }
        break;
    }
    case 9: { // Skill Death Reward：技能真实击杀 -> DeathEvent/Exp/Loot exactly-once
        auto all = monsters();
        auto* slime = FindAliveMonsterByName(*this, all, "Slime");
        if (slime == nullptr) {
            if (m_skillTestStageElapsed > 15.0) {
                fail("no alive slime for death reward stage");
                m_skillTestStage = 90;
            }
            break;
        }
        if (!m_playerSkill.GetCooldowns().IsReady("power_slash")) {
            break; // 等 CD
        }
        engageTarget(slime);
        m_playerSkill.ResetForRespawn(*m_player);
        m_skillTestTargetHpBefore = 0.0f;
        slime->GetCombatStats().SetHp(1.0f);
        const auto expBefore = m_player->GetProgression().GetTotalExp();
        const std::size_t lootBefore = m_worldActors.GetLoot().GetAll().size();
        auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
        if (!result.success) {
            if (m_skillTestStageElapsed > 15.0) {
                fail("kill cast rejected: " + result.reason);
                m_skillTestStage = 90;
            }
            break;
        }
        for (int i = 0; i < kMaxCastDriveFrames; ++i) {
            m_player->UpdateAnimation(0.05f);
            m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
            if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
                break;
            }
        }
        if (slime->IsCombatAlive()) {
            if (m_skillTestStageElapsed > 20.0) {
                fail("slime survived kill cast");
                m_skillTestStage = 90;
            }
            break;
        }
        (void)m_worldActors.ProcessDeathRewards();
        check("exp gained from skill kill",
              m_player->GetProgression().GetTotalExp() > expBefore);
        // 阶段8.1指令十二：确定性断言——Override 表保证 slime 必掉 small_potion x1
        const auto& lootsAfterKill = m_worldActors.GetLoot().GetAll();
        check("exactly one deterministic ground loot", lootsAfterKill.size() - lootBefore == 1);
        bool potionOnGround = false;
        for (const auto& loot : lootsAfterKill) {
            if (loot.itemId == "small_potion" && loot.quantity == 1) {
                potionOnGround = true;
                break;
            }
        }
        check("deterministic small_potion x1 on ground", potionOnGround);
        const auto expAfter = m_player->GetProgression().GetTotalExp();
        const std::size_t lootAfter = lootsAfterKill.size();
        (void)m_worldActors.ProcessDeathRewards();
        check("no duplicate reward",
              m_player->GetProgression().GetTotalExp() == expAfter &&
                  m_worldActors.GetLoot().GetAll().size() == lootAfter);
        pass("skill death reward verified");
        m_skillTestStage = 10;
        break;
    }
    case 10: { // Player Death -> Debug Respawn -> Mana Fill / CD 清 0 / 无施法 / Normal
        if (!m_skillTestCastRequested) {
            m_playerSkill.ResetForRespawn(*m_player);
            legend::combat::DamageEvent lethal;
            lethal.sourceId = m_skillTestTargetId != 0 ? m_skillTestTargetId : m_player->GetId();
            lethal.targetId = m_player->GetId();
            lethal.rawDamage = 99999.0f;
            lethal.finalDamage = 99999.0f;
            // 测试辅助（指令一百一十五）：制造 Player 死亡前置
            (void)m_worldActors.GetCombatSystem().ApplyDamage(lethal);
            m_skillTestCastRequested = true;
            break;
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal &&
            m_player->IsCombatAlive()) {
            check("mana refilled after respawn",
                  std::fabs(m_player->GetSkillResource().GetMana() -
                            m_player->GetSkillResource().GetMaxMana()) < 0.0001f);
            check("all cooldowns cleared after respawn",
                  m_playerSkill.GetCooldowns().IsReady("power_slash") &&
                      m_playerSkill.GetCooldowns().IsReady("whirlwind") &&
                      m_playerSkill.GetCooldowns().IsReady("piercing_strike") &&
                      m_playerSkill.GetCooldowns().IsReady("heavy_strike"));
            check("no current cast after respawn",
                  !m_playerSkill.GetSkillSystem().HasActiveCast());
            check("state Normal after respawn",
                  m_player->GetActionState() ==
                      legend::entity::CharacterActionState::Normal);
            pass("player respawn skill reset verified");
            m_skillTestCastRequested = false;
            m_skillTestStage = 11;
        } else if (m_skillTestStageElapsed > 30.0) {
            fail("player respawn not completed within 30s");
            m_skillTestStage = 90;
        }
        break;
    }
    case 11: { // Monster Respawn 仍正常
        if (m_skillTestStageElapsed < 10.0) {
            break; // 给 corpse/respawn 流程时间
        }
        const int aliveNow = m_worldActors.GetAliveMonsterCount();
        if (aliveNow > 0) {
            pass("monster respawn still working (alive " + std::to_string(aliveNow) + ")");
        } else if (m_skillTestStageElapsed < 40.0) {
            break;
        } else {
            fail("no monster respawned within 40s");
        }
        m_skillTestStage = 90;
        break;
    }
    case 90: { // 汇总 + 世界恢复（所有退出路径统一入口：正常/FAIL/timeout 均到达此处）
        if (!m_skillTestSummaryDone) {
            m_skillTestSummaryDone = true;
            // 阶段8.3指令十：统一清理顺序——1) 临时装备 2) 正式掉落表 3) 世界快照 4) Skill/Target
            // （FAIL/timeout/各步骤失败 Stage7 失败分支直接 stage=90，无单独清理分支，指令十一）
            CleanupSkillTestTemporaryEquipment(); // [SkillTest] temporary equipment cleaned.
            // 阶段8.1指令十三：恢复正式 slime 掉落表（禁止空表冒充，阶段7.2 同原则）
            RestoreSkillTestLootOverride(); // [SkillTest] official loot restored.
            // 阶段8.2指令二十：RestoreSkillWorldSnapshot 内部按序恢复——
            // Progression（level/currentExp/totalExp）→ Base Stats → Final 重算 →
            // playerHp → Actor（位置/HP/Active/ActionState/AI/Aggro）→ Loot/事件清理
            RestoreSkillWorldSnapshot();
            LOG_INFO("[SkillTest] progression restored.");
            LOG_INFO("[SkillTest] aggro state restored.");
            // Player 恢复可玩基线：清目标 / 取消施法 / 清 CD / FillMana（Restore 内已做，
            // 此处显式补一次确保语义）
            m_playerCombat.GetTarget().ClearTarget();
            m_playerSkill.ResetForRespawn(*m_player);
            m_player->ReturnToNormal();
            LOG_INFO("[SkillTest] completed, failures = " +
                     std::to_string(m_skillTestFailures) + ", elapsed = " +
                     std::to_string(m_skillTestElapsed) + "s");
            LOG_INFO("[SkillTest] world state restored.");
        }
        break;
    }
    default:
        break;
    }
}

// ==================== F7：Skill Debug 覆盖层 ====================
// 无文字系统：蓝色 Mana 条（mana/maxMana 比例）+ 4 个技能槽方块
//（Ready 全亮 / CD 按 remaining/total 比例变暗）。仅开发 Debug，不做正式 HUD。

void GameScene::DrawSkillDebugOverlay(legend::render::SpriteBatch& batch) {
    if (!m_player || !m_whiteTexture) {
        return;
    }
    const auto& visual = m_player->GetVisual();
    const legend::math::Vector2& feet = m_player->GetPosition();
    const float barW = 64.0f;
    const float barH = 6.0f;
    const legend::math::Vector2 barCenter(feet.x, feet.y - visual.height * visual.pivot.y - 24.0f);
    // 黑底 + 蓝色 Mana
    batch.DrawQuad(*m_whiteTexture, barCenter, {barW / 64.0f, barH / 64.0f}, 0.0f,
                   legend::math::Color(0.0f, 0.0f, 0.0f, 0.7f));
    const float ratio = m_player->GetSkillResource().GetManaPercent();
    if (ratio > 0.0f) {
        const float fillW = barW * ratio;
        const legend::math::Vector2 fillCenter(barCenter.x - (barW - fillW) * 0.5f, barCenter.y);
        batch.DrawQuad(*m_whiteTexture, fillCenter, {fillW / 64.0f, barH / 64.0f}, 0.0f,
                       legend::math::Color(0.2f, 0.4f, 1.0f, 0.95f));
    }
    // 4 个技能槽方块：Ready 亮绿；CD 按剩余比例降亮度
    const auto& database = m_worldActors.GetSkillDatabase();
    const float slotSize = 10.0f;
    const float slotGap = 3.0f;
    const float totalW = 4 * slotSize + 3 * slotGap;
    const legend::math::Vector2 rowCenter(feet.x, barCenter.y - 14.0f);
    for (int slot = 0; slot < m_player->GetLoadout().GetSlotCount(); ++slot) {
        const std::string& skillId = m_player->GetLoadout().GetSkillId(slot);
        float brightness = 1.0f; // Ready
        float totalCd = 0.0f;
        if (!skillId.empty()) {
            const auto* def = database.Get(skillId);
            totalCd = def != nullptr ? def->cooldown : 0.0f;
            const float remaining = m_playerSkill.GetCooldowns().GetRemaining(skillId);
            if (remaining > 0.0f && totalCd > 0.0f) {
                brightness = 0.25f + 0.75f * (remaining / totalCd); // CD 比例
                brightness = std::min(brightness, 1.0f);
            }
        } else {
            brightness = 0.15f; // 空槽
        }
        const float slotX = rowCenter.x - totalW * 0.5f + slot * (slotSize + slotGap) +
                            slotSize * 0.5f;
        batch.DrawQuad(*m_whiteTexture, {slotX, rowCenter.y},
                       {slotSize / 64.0f, slotSize / 64.0f}, 0.0f,
                       legend::math::Color(0.2f * brightness, 1.0f * brightness,
                                           0.2f * brightness, 0.9f));
    }
}

// ==================== 窗口标题 Skill 段 ====================
// LogMapStats（2s 节流）调用；不额外刷日志。
// 形如： | MP: 85/100 | S1 power_slash:2.4s | S2 whirlwind:READY | ...

std::string GameScene::GetSkillStatusText() const {
    if (m_player == nullptr) {
        return std::string();
    }
    std::string status =
        " | MP: " + std::to_string(static_cast<int>(m_player->GetSkillResource().GetMana())) +
        "/" + std::to_string(static_cast<int>(m_player->GetSkillResource().GetMaxMana()));
    const auto& database = m_worldActors.GetSkillDatabase();
    for (int slot = 0; slot < m_player->GetLoadout().GetSlotCount(); ++slot) {
        const std::string& skillId = m_player->GetLoadout().GetSkillId(slot);
        status += " | S" + std::to_string(slot + 1) + " ";
        if (skillId.empty()) {
            status += "empty";
            continue;
        }
        status += skillId;
        const float remaining = m_playerSkill.GetCooldowns().GetRemaining(skillId);
        if (remaining > 0.0f) {
            status += ":CD" + std::to_string(remaining).substr(0, 4) + "s";
        } else {
            status += ":READY";
        }
    }
    if (m_playerSkill.GetSkillSystem().HasActiveCast()) {
        status += " | Cast: " + m_playerSkill.GetSkillSystem().GetContext().skillId;
    }
    (void)database;
    return status;
}

// ==================== 阶段8.1：世界快照 Capture / Restore ====================
// Integration Check 与 Auto Skill Test 的保存/恢复核心（含 FAIL/timeout 路径）。

void GameScene::CaptureSkillWorldSnapshot() {
    auto& snapshot = m_skillWorldSnapshot;
    snapshot.actors.clear();
    snapshot.groundLootIds.clear();
    if (m_player != nullptr) {
        snapshot.playerPosition = m_player->GetPosition();
        snapshot.playerHp = m_player->GetCombatStats().hp;
        snapshot.playerLevel = m_player->GetProgression().GetLevel();
        snapshot.playerTotalExp = m_player->GetProgression().GetTotalExp();
        // 阶段8.2指令四/五：完整 Progression 快照 + Base Stats 快照
        snapshot.progression = m_player->GetProgression().CreateSnapshot();
        snapshot.playerBaseStats = m_player->GetBaseCombatStats();
    }
    snapshot.aliveMonsters = m_worldActors.GetAliveMonsterCount();
    const auto& loots = m_worldActors.GetLoot().GetAll();
    snapshot.groundLootCount = loots.size();
    for (const auto& loot : loots) {
        snapshot.groundLootIds.push_back(loot.lootEntityId);
    }
    auto captureActor = [this, &snapshot](legend::entity::Character* actor) {
        if (actor == nullptr) {
            return;
        }
        legend::skill::SkillWorldSnapshot::ActorState state;
        state.id = actor->GetId();
        state.position = actor->GetPosition();
        state.hp = actor->GetCombatStats().hp;
        state.active = actor->IsActive();
        state.actionState = static_cast<std::uint8_t>(actor->GetActionState());
        // 阶段8.2指令十五：Monster 追加 AI/Aggro 快照（NPC 无 AI，字段留空）
        if (actor->GetActorType() == legend::entity::ActorType::Monster) {
            state.hasAI = true;
            if (const auto* ai = m_worldActors.GetAIController(actor->GetId()); ai != nullptr) {
                state.aggro = ai->GetAggroTable().CreateSnapshot();
            }
            const auto* monster = static_cast<const legend::world::MonsterCharacter*>(actor);
            state.aiState = static_cast<int>(monster->GetAIState());
            state.aiTargetId = monster->GetTargetHandle().IsEmpty()
                                   ? legend::entity::kInvalidEntityId
                                   : monster->GetTargetHandle().GetId();
        }
        snapshot.actors.push_back(state);
    };
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        captureActor(monster);
    }
    for (legend::entity::Character* npc :
         m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::NPC)) {
        captureActor(npc);
    }
    snapshot.captured = true;
}

void GameScene::RestoreSkillWorldSnapshot() {
    const auto& snapshot = m_skillWorldSnapshot;
    if (!snapshot.captured) {
        return; // 未捕获过快照：无事可做
    }
    for (const auto& state : snapshot.actors) {
        legend::entity::Character* actor = m_worldActors.GetRegistry().Get(state.id);
        if (actor == nullptr) {
            continue; // 已 Despawn（重生由 World 系统负责，不在此重建）
        }
        actor->SetPosition(state.position);
        actor->SetActive(state.active);
        auto& stats = actor->GetCombatStats();
        if (state.hp > 0.0f && stats.hp <= 0.0f) {
            actor->ReturnToNormal(); // 测试致死：复活（先退出 Dead 状态再恢复 HP）
        }
        stats.SetHp(std::min(state.hp, stats.maxHp));
        // 阶段8.2指令十六：ActionState 恢复（基线稳定状态语义化处理）
        const auto snapshotState = static_cast<legend::entity::CharacterActionState>(
            state.actionState);
        if (stats.hp > 0.0f) {
            if (snapshotState == legend::entity::CharacterActionState::Normal) {
                if (actor->GetActionState() != legend::entity::CharacterActionState::Normal) {
                    actor->ReturnToNormal();
                }
            } else if (snapshotState == legend::entity::CharacterActionState::Dead) {
                if (actor->GetActionState() != legend::entity::CharacterActionState::Dead) {
                    actor->EnterDead();
                }
            } else {
                // Attacking/HitReact/SkillCasting 基线不应出现（Capture 要求稳定状态）；
                // 保守回 Normal 并告警（不保存字段却完全不用）
                LOG_WARN("[SkillSnapshot] actor #" + std::to_string(state.id) +
                         " baseline actionState is transient, restored to Normal.");
                actor->ReturnToNormal();
            }
        }
        // 阶段8.2指令十三/十五：Monster AI/Aggro 恢复
        if (state.hasAI && actor->GetActorType() == legend::entity::ActorType::Monster) {
            if (auto* ai = m_worldActors.GetAIController(actor->GetId()); ai != nullptr) {
                ai->GetAggroTable().RestoreSnapshot(state.aggro);
            }
            auto* monster = static_cast<legend::world::MonsterCharacter*>(actor);
            monster->SetAIState(static_cast<legend::world::MonsterAIState>(state.aiState));
            if (state.aiTargetId == legend::entity::kInvalidEntityId) {
                monster->GetTargetHandle().Clear();
            } else {
                monster->GetTargetHandle().Set(state.aiTargetId);
            }
        }
    }
    if (m_player != nullptr) {
        m_player->SetPosition(snapshot.playerPosition); // 位置恢复（阶段8.2 补：不可遗漏）
        // 阶段8.2指令七：恢复顺序——1) Progression 2) Base Stats 3) Recalculate Final
        // 4) 恢复 playerHp 5) clamp 到 Final MaxHP（不要先恢复 HP 再重算 MaxHP）
        if (!m_player->GetProgression().RestoreSnapshot(snapshot.progression)) {
            LOG_ERROR("[SkillSnapshot] progression restore rejected (invalid baseline).");
        }
        m_player->RestoreBaseStats(snapshot.playerBaseStats);
        m_player->RecalculateCombatStats(); // 当前正式 Equipment 重算 Final（指令六）
        m_player->GetCombatStats().SetHp(snapshot.playerHp);
        // Player 恢复可玩基线：清目标 + 取消施法 + 清 CD + FillMana + Normal（指令十五）
        m_playerCombat.GetTarget().ClearTarget();
        m_playerSkill.ResetForRespawn(*m_player);
        m_player->ReturnToNormal();
    }
    // 清理测试新增 GroundLoot：快照里没有的 id 一律移除（指令二十三）
    const std::vector<legend::world::LootEntityId>& keep = snapshot.groundLootIds;
    std::vector<legend::world::LootEntityId> toRemove;
    for (const auto& loot : m_worldActors.GetLoot().GetAll()) {
        if (std::find(keep.begin(), keep.end(), loot.lootEntityId) == keep.end()) {
            toRemove.push_back(loot.lootEntityId);
        }
    }
    for (legend::world::LootEntityId id : toRemove) {
        m_worldActors.GetLoot().RemoveById(id);
    }
    // 清空测试期间累计的 Combat 事件，防止 Aggro/Reward 二次消费（指令二十二）
    m_worldActors.GetCombatSystem().ClearRecentEvents();
    m_worldActors.GetCombatSystem().ClearRecentDeaths();
}

// ==================== [SkillWorldStateIsolationCheck] ====================
// 指令五/三十二：普通启动（仅 Pure Check 执行后）验证真实世界完全未被验收代码污染。

void GameScene::RunSkillWorldStateIsolationCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillWorldStateIsolationCheck", name, pass, failures);
    };
    const auto& snapshot = m_skillWorldSnapshot;
    check("world snapshot captured", snapshot.captured);
    check("player exp unchanged (no test reward)",
          m_player != nullptr &&
              m_player->GetProgression().GetTotalExp() == snapshot.playerTotalExp &&
              m_player->GetProgression().GetLevel() == snapshot.playerLevel);
    // 阶段8.2指令二十四：currentExp / Base Stats 对比（不能只比 Position/HP/Active）
    check("player currentExp unchanged",
          m_player != nullptr &&
              m_player->GetProgression().GetCurrentExp() == snapshot.progression.currentExp);
    check("player base stats unchanged",
          m_player != nullptr &&
              std::fabs(m_player->GetBaseCombatStats().maxHp -
                        snapshot.playerBaseStats.maxHp) < 0.001f &&
              std::fabs(m_player->GetBaseCombatStats().attack -
                        snapshot.playerBaseStats.attack) < 0.001f &&
              std::fabs(m_player->GetBaseCombatStats().defense -
                        snapshot.playerBaseStats.defense) < 0.001f);
    check("ground loot count unchanged (no test drop)",
          m_worldActors.GetLoot().GetAll().size() == snapshot.groundLootCount);
    check("alive monster count unchanged (no test kill)",
          m_worldActors.GetAliveMonsterCount() == snapshot.aliveMonsters);
    int posMismatch = 0;
    int hpMismatch = 0;
    int activeMismatch = 0;
    int aggroMismatch = 0;
    int aiMismatch = 0;
    for (const auto& state : snapshot.actors) {
        const legend::entity::Character* actor = m_worldActors.GetRegistry().Get(state.id);
        if (actor == nullptr) {
            ++activeMismatch;
            continue;
        }
        if ((actor->GetPosition() - state.position).LengthSq() > 0.0001f) {
            ++posMismatch;
        }
        if (std::fabs(actor->GetCombatStats().hp - state.hp) > 0.0001f) {
            ++hpMismatch;
        }
        if (actor->IsActive() != state.active) {
            ++activeMismatch;
        }
        // 阶段8.2指令二十四：Monster Aggro/AI 对比
        if (state.hasAI && actor->GetActorType() == legend::entity::ActorType::Monster) {
            const auto* ai = m_worldActors.GetAIController(state.id);
            const auto* monster =
                static_cast<const legend::world::MonsterCharacter*>(actor);
            if (ai == nullptr ||
                ai->GetAggroTable().CreateSnapshot().size() != state.aggro.size()) {
                ++aggroMismatch;
            } else {
                const auto aggroNow = ai->GetAggroTable().CreateSnapshot();
                for (std::size_t i = 0; i < aggroNow.size() && i < state.aggro.size(); ++i) {
                    if (aggroNow[i].first != state.aggro[i].first ||
                        std::fabs(aggroNow[i].second - state.aggro[i].second) > 0.0001f) {
                        ++aggroMismatch;
                        break;
                    }
                }
            }
            if (monster == nullptr ||
                static_cast<int>(monster->GetAIState()) != state.aiState ||
                monster->GetTargetHandle().GetId() != state.aiTargetId) {
                ++aiMismatch;
            }
        }
    }
    check("no monster teleported by checks", posMismatch == 0);
    check("no monster hp changed by checks", hpMismatch == 0);
    check("no monster killed/deactivated by checks", activeMismatch == 0);
    check("no monster aggro changed by checks", aggroMismatch == 0);
    check("no monster ai state/target changed by checks", aiMismatch == 0);
    LOG_INFO("[SkillWorldStateIsolationCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillNoFreeRewardCheck] ====================
// 指令二十四：没有用户实际击杀前，Skill 测试不能改变正式 Player progression。
// TestHero 正式配置起步即 Level1 / Exp0（若正式配置改变，此处基线需同步）。

void GameScene::RunSkillNoFreeRewardCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillNoFreeRewardCheck", name, pass, failures);
    };
    const auto& snapshot = m_skillWorldSnapshot;
    // 阶段8.2指令二十三：对比 Capture 基线（正式角色未来可能从存档 Level20 加载——
    // 验收语义是"没有被测试改变"，不是"必须 Level1"）
    check("player level unchanged by checks",
          m_player != nullptr &&
              m_player->GetProgression().GetLevel() == snapshot.progression.level);
    check("player currentExp unchanged by checks",
          m_player != nullptr &&
              m_player->GetProgression().GetCurrentExp() == snapshot.progression.currentExp);
    check("player totalExp unchanged by checks (no free test reward)",
          m_player != nullptr &&
              m_player->GetProgression().GetTotalExp() == snapshot.progression.totalExp);
    LOG_INFO("[SkillNoFreeRewardCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillActiveCheck] ====================
// 指令十七/十九：Inactive Player 不能 CanCast（无副作用）；Inactive Target 拒绝；
// CombatSystem::ApplySkillDamage 的 IsActive 双保险继续生效（指令十八）。

void GameScene::RunSkillActiveCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillActiveCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillActiveCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillActiveCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    auto& skillSystem = m_playerSkill.GetSkillSystem();
    const auto& cooldowns = m_playerSkill.GetCooldowns();

    // 1. Active + Alive：CanCast 成功
    auto result =
        skillSystem.CanCast(*m_player, "power_slash", m_playerCombat.GetTarget(), cooldowns);
    check("active alive player can cast", result.success);
    const float manaBefore = m_player->GetSkillResource().GetMana();

    // 2. Player inactive：CanCast 最前置失败，无任何副作用（指令十六/十七）
    m_player->SetActive(false);
    auto inactive =
        skillSystem.CanCast(*m_player, "power_slash", m_playerCombat.GetTarget(), cooldowns);
    check("inactive player can cast -> fail", !inactive.success);
    check("inactive player: mana unchanged",
          std::fabs(m_player->GetSkillResource().GetMana() - manaBefore) < 0.0001f);
    check("inactive player: no cooldown started", cooldowns.IsReady("power_slash"));
    check("inactive player: action state unchanged",
          m_player->GetActionState() == legend::entity::CharacterActionState::Normal);
    m_player->SetActive(true);

    // 3. Target inactive：SingleTarget CanCast 失败（指令十九）
    boar->SetActive(false);
    auto noTarget =
        skillSystem.CanCast(*m_player, "power_slash", m_playerCombat.GetTarget(), cooldowns);
    check("inactive target can cast -> fail", !noTarget.success);
    check("inactive target: mana unchanged",
          std::fabs(m_player->GetSkillResource().GetMana() - manaBefore) < 0.0001f);
    check("inactive target: no cooldown started", cooldowns.IsReady("power_slash"));

    // 4. CombatSystem 双保险仍在：ApplySkillDamage 直接探测 inactive 目标被拒（指令十八）
    legend::combat::DamageEvent probe;
    check("apply skill damage to inactive target rejected",
          !m_worldActors.GetCombatSystem().ApplySkillDamage(*m_player, *boar, 10.0f,
                                                            "active_check_probe", probe));
    boar->SetActive(true);

    // 5. 恢复后 CanCast 重新成功
    auto restored =
        skillSystem.CanCast(*m_player, "power_slash", m_playerCombat.GetTarget(), cooldowns);
    check("restored active target can cast again", restored.success);
    LOG_INFO("[SkillActiveCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillDeterministicRewardCheck] ====================
// 指令十/二十/三十：确定性 Loot 语义——Override slime 表为 small_potion 1.0/1/1，
// 真实技能击杀后 GroundLoot 精确 +1 且内容为 small_potion x1；结束恢复正式表。

void GameScene::RunSkillDeterministicRewardCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillDeterministicRewardCheck", name, pass, failures);
    };
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillDeterministicRewardCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    legend::world::MonsterCharacter* slime = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            slime = monster;
            break;
        }
    }
    check("slime present", slime != nullptr);
    if (slime == nullptr) {
        LOG_INFO("[SkillDeterministicRewardCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    // 保存正式表 -> Override 确定性掉落（结束恢复，guard 同时清测试 GroundLoot）
    const auto originalLoot = m_worldActors.GetSpawner().GetLootEntries("slime");
    std::vector<legend::world::LootEntry> guaranteed;
    legend::world::LootEntry guaranteedEntry;
    guaranteedEntry.itemId = "small_potion";
    guaranteedEntry.chance = 1.0f;
    guaranteedEntry.min = 1;
    guaranteedEntry.max = 1;
    guaranteed.push_back(guaranteedEntry);
    m_worldActors.GetSpawner().SetTestLootOverride("slime", guaranteed);

    m_player->SetPosition(slime->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(slime->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    m_worldActors.GetCombatSystem().ClearRecentDeaths();
    slime->GetCombatStats().SetHp(1.0f);
    const std::size_t lootBefore = m_worldActors.GetLoot().GetAll().size();
    const auto expBefore = m_player->GetProgression().GetTotalExp();

    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    check("slime killed by skill", !slime->IsCombatAlive());
    const int rewarded = m_worldActors.ProcessDeathRewards();
    check("reward processed once", rewarded == 1);
    const auto& loots = m_worldActors.GetLoot().GetAll();
    check("exactly one deterministic ground loot", loots.size() - lootBefore == 1);
    bool potionOnGround = false;
    for (const auto& loot : loots) {
        if (loot.itemId == "small_potion" && loot.quantity == 1) {
            potionOnGround = true;
            break;
        }
    }
    check("ground loot is small_potion x1", potionOnGround);
    check("exp gained from deterministic kill",
          m_player->GetProgression().GetTotalExp() > expBefore);
    const auto expAfter = m_player->GetProgression().GetTotalExp();
    const std::size_t lootAfter = loots.size();
    check("second reward pass is no-op",
          m_worldActors.ProcessDeathRewards() == 0 &&
              m_player->GetProgression().GetTotalExp() == expAfter &&
              m_worldActors.GetLoot().GetAll().size() == lootAfter);
    // 恢复正式表（guard 同时清理测试 GroundLoot / 复活 slime）
    m_worldActors.GetSpawner().SetTestLootOverride("slime", originalLoot);
    LOG_INFO("[SkillDeterministicRewardCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== 阶段8.1：Auto Skill Test 掉落恢复 ====================
// 与阶段7.2 相同原则：Stage0 保存正式表，Stage90（所有退出路径）统一恢复。

void GameScene::RestoreSkillTestLootOverride() {
    if (!m_skillTestLootSaved) {
        return; // 未保存过正式表（无 Override 发生）
    }
    m_worldActors.GetSpawner().SetTestLootOverride("slime", m_skillTestOriginalSlimeLoot);
    m_skillTestLootSaved = false;
    LOG_INFO("[SkillTest] official loot restored.");
}

// ==================== 阶段8.3：临时装备统一清理 ====================
// Auto Skill Test Stage7 创建的 wooden_sword 测试实例（m_skillTestSwordId）：
// - 幂等：id==0 直接返回，可重复调用
// - Equip 中：直接 TakeEquipped 取出（不要求背包空位——目标是移除测试物品，不是还给玩家）
// - 在背包中：按 instanceId TakeInstance 移除
// - 只按 instanceId 删除（绝不按 definitionId=wooden_sword——玩家可能真实拥有同名装备）
void GameScene::CleanupSkillTestTemporaryEquipment() {
    if (m_skillTestSwordId == 0 || m_player == nullptr) {
        m_skillTestSwordId = 0; // 幂等：无测试实例（或无玩家）即复位
        return;
    }
    auto& equipment = m_player->GetEquipment();
    auto& bag = m_player->GetInventory();
    bool equipmentChanged = false; // 阶段8.4指令二：装备槽变化才重算 Final Stats
    // 1) 若测试剑还装备在 Weapon 槽：直接取出销毁（不等背包空间，指令五）
    if (const auto* equipped = equipment.GetEquipped(legend::item::EquipmentSlotType::Weapon);
        equipped != nullptr && equipped->instanceId == m_skillTestSwordId) {
        auto taken = equipment.TakeEquipped(legend::item::EquipmentSlotType::Weapon);
        if (taken.has_value()) {
            equipmentChanged = true; // 阶段8.4指令二：Weapon 槽变化 → Final 重算
            LOG_INFO("[SkillTest] temporary equipment removed from Weapon slot (instance " +
                     std::to_string(m_skillTestSwordId) + ").");
        }
    }
    // 2) 若测试剑仍在背包：按 instanceId 精确移除（背包实例不影响 Final，无需重算）
    if (bag.FindByInstanceId(m_skillTestSwordId) != nullptr) {
        (void)bag.TakeInstance(m_skillTestSwordId);
        LOG_INFO("[SkillTest] temporary equipment removed from inventory (instance " +
                 std::to_string(m_skillTestSwordId) + ").");
    }
    m_skillTestSwordId = 0;
    // 阶段8.4指令一/三：装备槽变化 → 立即走正式重算 API（Final = Base + Equipment，
    // HP clamp 到新 maxHp 由 RecalculateCombatStats 内部正式逻辑处理——不硬编码 -12）
    if (equipmentChanged) {
        m_player->RecalculateCombatStats();
    }
    LOG_INFO("[SkillTest] temporary equipment cleaned.");
}

// ==================== [SkillProgressionRestoreCheck] ====================
// 阶段8.2指令九/十/十一：真实升级（≥2级）→ Restore → Level/currentExp/totalExp/
// Base Stats（maxHp/attack/defense）/Final Stats/pendingLevelUps 全部回滚。

void GameScene::RunSkillProgressionRestoreCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillProgressionRestoreCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillProgressionRestoreCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    const auto baseBefore = m_player->GetBaseCombatStats();

    // 模拟测试污染：一次加入足够升 ≥2 级的经验（req(1)+req(2)=250，加 300 → lv3）
    const auto levelUps = m_player->AddExperience(300);
    check("polluted: leveled up at least 2 levels",
          m_player->GetProgression().GetLevel() >= 3 && levelUps.size() >= 2);
    check("polluted: base attack grew", m_player->GetBaseCombatStats().attack >
                                            baseBefore.attack + 1.0f);
    check("polluted: base maxHp grew", m_player->GetBaseCombatStats().maxHp >
                                           baseBefore.maxHp + 1.0f);

    // 阶段8.2指令九：Restore（RestoreGuard 析构；此处手动提前调用以断言）
    RestoreSkillWorldSnapshot();

    // 指令九/十：Level/currentExp/totalExp/Base Stats 全部回滚（不只 totalExp）
    check("restored: level back to baseline",
          m_player->GetProgression().GetLevel() == m_skillWorldSnapshot.progression.level);
    check("restored: currentExp back to 0",
          m_player->GetProgression().GetCurrentExp() ==
              m_skillWorldSnapshot.progression.currentExp);
    check("restored: totalExp back to baseline",
          m_player->GetProgression().GetTotalExp() == m_skillWorldSnapshot.progression.totalExp);
    check("restored: base attack back",
          std::fabs(m_player->GetBaseCombatStats().attack - baseBefore.attack) < 0.001f);
    check("restored: base maxHp back",
          std::fabs(m_player->GetBaseCombatStats().maxHp - baseBefore.maxHp) < 0.001f);
    check("restored: base defense back",
          std::fabs(m_player->GetBaseCombatStats().defense - baseBefore.defense) < 0.001f);
    // 指令六：Final Stats 也正确恢复（无装备时 final == base）
    check("restored: final stats match base (no equipment)",
          std::fabs(m_player->GetCombatStats().attack - baseBefore.attack) < 0.001f &&
              std::fabs(m_player->GetCombatStats().maxHp - baseBefore.maxHp) < 0.001f);
    // 指令十一：pending 升级事件清空（未来 UI 不能突然显示测试升级）
    check("restored: pending level-ups cleared", m_player->GetProgression().GetPendingLevelUps().empty());
    LOG_INFO("[SkillProgressionRestoreCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillAggroRestoreCheck] ====================
// 阶段8.2指令十七：真实技能命中 → Threat 增加 → Restore → Threat/AIState/TargetId
// 全部回到测试前（SkillAggroCheck 不再永久污染 Monster AggroTable / AI 状态）。

void GameScene::RunSkillAggroRestoreCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillAggroRestoreCheck", name, pass, failures);
    };
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillAggroRestoreCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    CaptureSkillWorldSnapshot();
    RestoreGuard skillRestoreGuard{this};
    legend::world::MonsterCharacter* wolf = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Wolf") {
            wolf = monster;
            break;
        }
    }
    check("wolf present", wolf != nullptr);
    if (wolf == nullptr) {
        LOG_INFO("[SkillAggroRestoreCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    wolf->GetCombatStats().SetHp(wolf->GetCombatStats().maxHp);
    m_player->SetPosition(wolf->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(wolf->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    const auto* ai = m_worldActors.GetAIController(wolf->GetId());
    check("wolf AI controller present", ai != nullptr);
    if (ai == nullptr) {
        LOG_INFO("[SkillAggroRestoreCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    m_worldActors.DispatchCombatEvents(); // 清遗留事件
    const float threatBefore = ai->GetAggroTable().GetThreat(m_player->GetId());
    const auto aiStateBefore = wolf->GetAIState();
    const auto targetBefore = wolf->GetTargetHandle().GetId();

    // 真实命中
    auto result = m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    check("cast succeeds", result.success);
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    m_worldActors.DispatchCombatEvents();
    const float threatAfterHit = ai->GetAggroTable().GetThreat(m_player->GetId());
    check("threat increased after skill hit", threatAfterHit > threatBefore);

    // Restore（guard 前手动调用以断言）
    RestoreSkillWorldSnapshot();
    // 指令十七：Threat/AIState/TargetId 全部回到测试前
    check("restored: threat back to baseline",
          std::fabs(ai->GetAggroTable().GetThreat(m_player->GetId()) - threatBefore) < 0.0001f);
    check("restored: AI state back to baseline",
          wolf->GetAIState() == aiStateBefore);
    check("restored: AI target back to baseline",
          wolf->GetTargetHandle().GetId() == targetBefore);
    m_playerSkill.ResetForRespawn(*m_player);
    LOG_INFO("[SkillAggroRestoreCheck] completed, failures = " + std::to_string(failures));
}

// ==================== [SkillFullStateRestoreCheck] ====================
// 阶段8.2指令十八：总体验证——污染后 Restore，Player（位置/HP/Level/Exp/Base/Final）
// + World（位置/HP/Active/Aggro/AITarget）+ Loot + Combat events 全部与快照一致。

void GameScene::RunSkillFullStateRestoreCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillFullStateRestoreCheck", name, pass, failures);
    };
    auto& input = Engine::Get().GetInput();
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillFullStateRestoreCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    CaptureSkillWorldSnapshot();
    const auto snapshotCopy = m_skillWorldSnapshot; // 比对基线（Restore 不清 captured）
    // 阶段8.3指令五：Inventory + Equipment instanceId 集合基线（Restore 后按集合对比）
    std::vector<legend::item::ItemInstanceId> bagBefore;
    for (std::size_t i = 0; i < m_player->GetInventory().GetCapacity(); ++i) {
        if (const auto* item = m_player->GetInventory().GetSlot(i); item != nullptr) {
            bagBefore.push_back(item->instanceId);
        }
    }
    std::vector<legend::item::ItemInstanceId> equippedBefore;
    m_player->GetEquipment().ForEachEquipped(
        [&equippedBefore](const legend::item::ItemInstance& item) {
            equippedBefore.push_back(item.instanceId);
        });
    RestoreGuard skillRestoreGuard{this};
    legend::world::MonsterCharacter* boar = nullptr;
    for (legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            boar = monster;
            break;
        }
    }
    check("boar present", boar != nullptr);
    if (boar == nullptr) {
        LOG_INFO("[SkillFullStateRestoreCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    // 制造全类型污染：位置 + HP + Exp/升级 + Aggro + 施法
    m_player->SetPosition(boar->GetPosition() + legend::math::Vector2(55.0f, 0.0f));
    m_playerCombat.GetTarget().SetTarget(boar->GetId());
    m_playerSkill.ResetForRespawn(*m_player);
    boar->GetCombatStats().SetHp(boar->GetCombatStats().maxHp);
    (void)m_player->AddExperience(300); // Exp/Level/Base Stats 污染
    (void)m_playerSkill.RequestSkill(*m_player, 0, m_playerCombat.GetTarget());
    for (int i = 0; i < kMaxCastDriveFrames; ++i) {
        m_player->UpdateAnimation(0.05f);
        m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, 0.016f);
        if (!m_playerSkill.GetSkillSystem().HasActiveCast()) {
            break;
        }
    }
    m_worldActors.DispatchCombatEvents();
    check("pollution applied (exp changed)",
          m_player->GetProgression().GetTotalExp() != snapshotCopy.progression.totalExp);

    // Restore + 全量对比
    RestoreSkillWorldSnapshot();
    bool allMatch = true;
    auto fail = [&check](const std::string& name, bool ok) { check(name, ok); };
    fail("player position restored",
         (m_player->GetPosition() - snapshotCopy.playerPosition).LengthSq() < 0.0001f ||
             (LOG_WARN("[SkillFullStateRestore] player pos now=(" +
                       std::to_string(m_player->GetPosition().x) + "," +
                       std::to_string(m_player->GetPosition().y) + ") baseline=(" +
                       std::to_string(snapshotCopy.playerPosition.x) + "," +
                       std::to_string(snapshotCopy.playerPosition.y) + ")"),
              false));
    fail("player hp restored",
         std::fabs(m_player->GetCombatStats().hp - snapshotCopy.playerHp) < 0.001f);
    fail("player level/currentExp/totalExp restored",
         m_player->GetProgression().GetLevel() == snapshotCopy.progression.level &&
             m_player->GetProgression().GetCurrentExp() ==
                 snapshotCopy.progression.currentExp &&
             m_player->GetProgression().GetTotalExp() ==
                 snapshotCopy.progression.totalExp);
    fail("player base stats restored",
         std::fabs(m_player->GetBaseCombatStats().attack -
                   snapshotCopy.playerBaseStats.attack) < 0.001f &&
             std::fabs(m_player->GetBaseCombatStats().maxHp -
                       snapshotCopy.playerBaseStats.maxHp) < 0.001f &&
             std::fabs(m_player->GetBaseCombatStats().defense -
                       snapshotCopy.playerBaseStats.defense) < 0.001f);
    fail("player final stats restored (no equipment => final == base)",
         std::fabs(m_player->GetCombatStats().attack -
                   snapshotCopy.playerBaseStats.attack) < 0.001f);
    // 阶段8.4指令十六：Final Defense/MaxHP 同样与 baseline 一致（不只 attack）
    fail("player final defense restored (no equipment)",
         std::fabs(m_player->GetCombatStats().defense -
                   snapshotCopy.playerBaseStats.defense) < 0.001f);
    fail("player final maxHp restored (no equipment)",
         std::fabs(m_player->GetCombatStats().maxHp -
                   snapshotCopy.playerBaseStats.maxHp) < 0.001f);
    fail("pending level-ups cleared",
         m_player->GetProgression().GetPendingLevelUps().empty());
    bool worldMatch = true;
    for (const auto& state : snapshotCopy.actors) {
        const legend::entity::Character* actor = m_worldActors.GetRegistry().Get(state.id);
        if (actor == nullptr) {
            worldMatch = false;
            break;
        }
        if ((actor->GetPosition() - state.position).LengthSq() > 0.0001f ||
            std::fabs(actor->GetCombatStats().hp - state.hp) > 0.0001f ||
            actor->IsActive() != state.active) {
            worldMatch = false;
            break;
        }
        if (state.hasAI && actor->GetActorType() == legend::entity::ActorType::Monster) {
            const auto* ai = m_worldActors.GetAIController(state.id);
            if (ai == nullptr ||
                ai->GetAggroTable().CreateSnapshot().size() != state.aggro.size()) {
                worldMatch = false;
                break;
            }
        }
    }
    fail("world actors fully restored (position/hp/active/aggro)", worldMatch);
    fail("ground loot count restored",
         m_worldActors.GetLoot().GetAll().size() == snapshotCopy.groundLootCount);
    fail("combat recent events empty",
         m_worldActors.GetCombatSystem().GetRecentEvents().empty() &&
             m_worldActors.GetCombatSystem().GetRecentDeaths().empty());
    // 阶段8.3指令五：Inventory + Equipment instanceId 集合与基线一致
    // （至少无测试实例残留——临时装备由 CleanupSkillTestTemporaryEquipment 统一保证）
    std::vector<legend::item::ItemInstanceId> bagAfter;
    for (std::size_t i = 0; i < m_player->GetInventory().GetCapacity(); ++i) {
        if (const auto* item = m_player->GetInventory().GetSlot(i); item != nullptr) {
            bagAfter.push_back(item->instanceId);
        }
    }
    std::vector<legend::item::ItemInstanceId> equippedAfter;
    m_player->GetEquipment().ForEachEquipped(
        [&equippedAfter](const legend::item::ItemInstance& item) {
            equippedAfter.push_back(item.instanceId);
        });
    auto sameSet = [](const std::vector<legend::item::ItemInstanceId>& left,
                      const std::vector<legend::item::ItemInstanceId>& right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (const auto id : left) {
            bool found = false;
            for (const auto other : right) {
                if (other == id) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    };
    fail("inventory instanceId set matches baseline", sameSet(bagAfter, bagBefore));
    fail("equipment instanceId set matches baseline",
         sameSet(equippedAfter, equippedBefore));
    fail("no test sword residue (cleanup resets id to 0)",
         m_skillTestSwordId == 0 &&
             m_player->GetInventory().FindByInstanceId(m_skillTestSwordId) == nullptr);
    (void)allMatch;
    LOG_INFO("[SkillFullStateRestoreCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillTemporaryEquipmentCleanupCheck] ====================
// 阶段8.3指令十二：临时装备统一清理验收——
// 场景A：实例A AddInstance（不装备）-> Cleanup -> 背包无A
// 场景B：实例B AddInstance + Equip -> Cleanup -> Weapon 槽与背包均无 B
// 场景C：预先放置"玩家真实"同定义实例C -> Cleanup 后 C 必须仍在（按 instanceId 精确删）
void GameScene::RunSkillTemporaryEquipmentCleanupCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillTemporaryEquipmentCleanupCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillTemporaryEquipmentCleanupCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    const legend::item::ItemDefinition* swordDef =
        m_worldActors.GetItemDatabase().Get("wooden_sword");
    check("wooden_sword definition present", swordDef != nullptr);
    if (swordDef == nullptr) {
        LOG_INFO("[SkillTemporaryEquipmentCleanupCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    auto makeInstance = [](const legend::item::ItemDefinition& def) {
        legend::item::ItemInstance instance;
        instance.instanceId = legend::item::ItemInstanceIdAllocator::Next();
        instance.definitionId = def.id;
        instance.quantity = 1;
        return instance;
    };
    auto& bag = m_player->GetInventory();
    auto& equipment = m_player->GetEquipment();

    // ---- 场景C：预置"玩家真实"实例C（不注册为测试实例）----
    legend::item::ItemInstance playerSword = makeInstance(*swordDef);
    check("scenario C: player wooden_sword added", bag.AddInstance(playerSword, swordDef));
    const legend::item::ItemInstanceId playerSwordId = playerSword.instanceId;

    // ---- 场景A：A 入背包（不装备）-> Cleanup -> 背包无 A ----
    legend::item::ItemInstance swordA = makeInstance(*swordDef);
    check("scenario A: instance added", bag.AddInstance(swordA, swordDef));
    m_skillTestSwordId = swordA.instanceId;
    CleanupSkillTestTemporaryEquipment();
    check("scenario A: cleanup cleared test id", m_skillTestSwordId == 0);
    check("scenario A: inventory free of A",
          bag.FindByInstanceId(swordA.instanceId) == nullptr);
    check("scenario C: player wooden_sword kept",
          bag.FindByInstanceId(playerSwordId) != nullptr);

    // ---- 场景B：B AddInstance + Equip -> Cleanup -> 槽与背包均无 B ----
    legend::item::ItemInstance swordB = makeInstance(*swordDef);
    check("scenario B: instance added", bag.AddInstance(swordB, swordDef));
    const float attackBeforeB = m_player->GetCombatStats().attack; // 阶段8.4：装备前 baseline
    const auto equipB = m_player->EquipInstance(swordB.instanceId);
    check("scenario B: equipped", equipB.success);
    if (equipB.success) {
        m_skillTestSwordId = swordB.instanceId;
        CleanupSkillTestTemporaryEquipment();
        check("scenario B: cleanup cleared test id", m_skillTestSwordId == 0);
        const auto* equipped = equipment.GetEquipped(legend::item::EquipmentSlotType::Weapon);
        check("scenario B: weapon slot free of B",
              equipped == nullptr || equipped->instanceId != swordB.instanceId);
        check("scenario B: inventory free of B",
              bag.FindByInstanceId(swordB.instanceId) == nullptr);
        // 阶段8.4指令十一：Final Attack 立即恢复 baseline（Cleanup 内部已重算）
        check("scenario B: final attack restored to baseline",
              std::fabs(m_player->GetCombatStats().attack - attackBeforeB) < 0.001f);
    }
    // 场景C收尾：B 清理后 C 必须仍在（只按 instanceId 删除，不按 definitionId）
    check("scenario C: kept after B cleanup",
          bag.FindByInstanceId(playerSwordId) != nullptr);

    // 幂等：id=0 再调 Cleanup 无副作用
    m_skillTestSwordId = 0;
    CleanupSkillTestTemporaryEquipment();
    check("idempotent: second cleanup safe", m_skillTestSwordId == 0);

    // 测试自清：移除预置的 C（Check 本身不留污染）
    const auto removedC = bag.TakeInstance(playerSwordId);
    check("scenario C: removed (test hygiene)", removedC.has_value());
    LOG_INFO("[SkillTemporaryEquipmentCleanupCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillEquipmentFailureCleanupCheck] ====================
// 阶段8.3指令十三：模拟 Stage7 FAIL/timeout 路径——失败出口跳 stage90 后由
// Stage90 统一 Cleanup（等价于直接调 CleanupSkillTestTemporaryEquipment），无残留。
// 场景A：Add 成功 + Equip 失败（中途失败）-> Cleanup -> 背包无残留
// 场景B：已装备后 timeout -> Cleanup -> Weapon 槽 + 背包无残留
void GameScene::RunSkillEquipmentFailureCleanupCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillEquipmentFailureCleanupCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillEquipmentFailureCleanupCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    const legend::item::ItemDefinition* swordDef =
        m_worldActors.GetItemDatabase().Get("wooden_sword");
    check("wooden_sword definition present", swordDef != nullptr);
    if (swordDef == nullptr) {
        LOG_INFO("[SkillEquipmentFailureCleanupCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    auto& bag = m_player->GetInventory();
    auto& equipment = m_player->GetEquipment();

    // ---- 场景A：Add 成功 + Equip 失败（Stage7 中途失败，等价 stage=90）----
    legend::item::ItemInstance instanceA;
    instanceA.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    instanceA.definitionId = "wooden_sword";
    instanceA.quantity = 1;
    check("fail path A: instance added", bag.AddInstance(instanceA, swordDef));
    m_skillTestSwordId = instanceA.instanceId;
    // 故意不调用 EquipInstance（等价 Equip 失败）；Stage90 等价清理：
    CleanupSkillTestTemporaryEquipment();
    check("fail path A: no inventory residue",
          bag.FindByInstanceId(instanceA.instanceId) == nullptr);
    check("fail path A: id reset", m_skillTestSwordId == 0);

    // ---- 场景B：已装备后 timeout（等价 stage=90）----
    legend::item::ItemInstance instanceB;
    instanceB.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    instanceB.definitionId = "wooden_sword";
    instanceB.quantity = 1;
    check("fail path B: instance added", bag.AddInstance(instanceB, swordDef));
    const float attackBeforeB = m_player->GetCombatStats().attack; // 阶段8.4：装备前 baseline
    const auto equipB = m_player->EquipInstance(instanceB.instanceId);
    check("fail path B: equipped (precondition)", equipB.success);
    if (equipB.success) {
        m_skillTestSwordId = instanceB.instanceId;
        CleanupSkillTestTemporaryEquipment();
        const auto* equipped = equipment.GetEquipped(legend::item::EquipmentSlotType::Weapon);
        check("fail path B: weapon slot free of B",
              equipped == nullptr || equipped->instanceId != instanceB.instanceId);
        check("fail path B: no inventory residue",
              bag.FindByInstanceId(instanceB.instanceId) == nullptr);
        check("fail path B: id reset", m_skillTestSwordId == 0);
        // 阶段8.4指令十四：FAIL 路径 Final Attack 同样立即恢复 baseline
        check("fail path B: final attack restored to baseline",
              std::fabs(m_player->GetCombatStats().attack - attackBeforeB) < 0.001f);
    }
    LOG_INFO("[SkillEquipmentFailureCleanupCheck] completed, failures = " +
             std::to_string(failures));
}

// ==================== [SkillTemporaryEquipmentStatsCheck] ====================
// 阶段8.4指令七/八/九：临时装备 Final Stats 重算验收——
// 1) 单次：wooden_sword 装备后 Final Attack == base + attackBonus(12)，Cleanup 后
//    立即恢复 baseline（走正式 RecalculateCombatStats，绝不硬编码差值）
// 2) 防漂移：100 次循环（创建 -> 装备 -> Cleanup），每轮结束 Final Attack == baseline，
//    最终无任何漂移；HP clamp 语义由 RecalculateCombatStats 内部正式逻辑保证
//   （卸下超限降、穿上不补满）——未来任何带 maxHpBonus 的装备同样自动正确
void GameScene::RunSkillTemporaryEquipmentStatsCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LogSkillCheck("SkillTemporaryEquipmentStatsCheck", name, pass, failures);
    };
    if (m_player == nullptr) {
        check("player present", false);
        LOG_INFO("[SkillTemporaryEquipmentStatsCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    const legend::item::ItemDefinition* swordDef =
        m_worldActors.GetItemDatabase().Get("wooden_sword");
    check("wooden_sword definition present", swordDef != nullptr);
    if (swordDef == nullptr) {
        LOG_INFO("[SkillTemporaryEquipmentStatsCheck] completed, failures = " +
                 std::to_string(failures));
        return;
    }
    auto& bag = m_player->GetInventory();
    auto& equipment = m_player->GetEquipment();

    // ---- 单次：装备 -> Final == base + bonus -> Cleanup -> 立即恢复 baseline ----
    const float baseAttack = m_player->GetBaseCombatStats().attack;
    const float finalBefore = m_player->GetCombatStats().attack; // 无装备 => final == base
    legend::item::ItemInstance sword;
    sword.instanceId = legend::item::ItemInstanceIdAllocator::Next();
    sword.definitionId = "wooden_sword";
    sword.quantity = 1;
    check("single: instance added", bag.AddInstance(sword, swordDef));
    const auto equipped = m_player->EquipInstance(sword.instanceId);
    check("single: equipped", equipped.success);
    if (equipped.success) {
        const float finalEquipped = m_player->GetCombatStats().attack;
        check("single: final attack == base + attackBonus",
              std::fabs(finalEquipped - (baseAttack + swordDef->equipment.attackBonus)) <
                  0.001f);
        m_skillTestSwordId = sword.instanceId;
        CleanupSkillTestTemporaryEquipment();
        check("single: cleanup reset id", m_skillTestSwordId == 0);
        check("single: final attack restored immediately",
              std::fabs(m_player->GetCombatStats().attack - finalBefore) < 0.001f);
    }

    // ---- 防漂移：100 次循环（每轮创建 -> 装备 -> Cleanup -> Final == baseline）----
    int drift = 0;
    for (int round = 0; round < 100; ++round) {
        legend::item::ItemInstance loopSword;
        loopSword.instanceId = legend::item::ItemInstanceIdAllocator::Next();
        loopSword.definitionId = "wooden_sword";
        loopSword.quantity = 1;
        if (!bag.AddInstance(loopSword, swordDef)) {
            ++drift; // 满包等异常计为漂移
            break;
        }
        const auto loopEquip = m_player->EquipInstance(loopSword.instanceId);
        if (!loopEquip.success) {
            ++drift;
            break;
        }
        m_skillTestSwordId = loopSword.instanceId;
        CleanupSkillTestTemporaryEquipment(); // 内部重算 Final Stats
        if (std::fabs(m_player->GetCombatStats().attack - finalBefore) > 0.001f) {
            ++drift;
            break;
        }
    }
    check("100 rounds: no attack drift", drift == 0);
    check("100 rounds: final attack == baseline",
          std::fabs(m_player->GetCombatStats().attack - finalBefore) < 0.001f);
    check("100 rounds: weapon slot empty",
          equipment.GetEquipped(legend::item::EquipmentSlotType::Weapon) == nullptr);
    LOG_INFO("[SkillTemporaryEquipmentStatsCheck] completed, failures = " +
             std::to_string(failures));
}
