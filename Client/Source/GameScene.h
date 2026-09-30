#pragma once

#include <memory>
#include <vector>

#include "Client/Character/PlayerCharacter.h"
#include "Client/Character/PlayerController.h"
#include "Client/Combat/PlayerCombatController.h"
#include "Client/Skill/PlayerSkillController.h"
#include "Client/Skill/SkillWorldSnapshot.h"
#include "Client/World/WorldActorManager.h"
#include "Engine/Entity/CharacterController.h"
#include "Engine/Entity/EntityId.h"
#include "Engine/Map/MapRenderer.h"
#include "Engine/Math/Color.h"
#include "Engine/Render/CharacterRenderer.h"
#include "Engine/Scene/Scene.h"

// 数据驱动游戏场景：Map（Tile/Object/Collision/Occlusion）+ Character Entity 体系。
// 管线：InputManager -> PlayerController -> CharacterController -> Character -> Map Collision -> Position
namespace legend::client {
class ClientNetworkController; // 阶段9：网络控制器（GameScene 只调 Update，指令六）
class VisualRuntime;           // 阶段24：视觉运行时（真实资源渲染门面）
}

class GameScene final : public legend::scene::Scene {
public:
    explicit GameScene(std::shared_ptr<legend::map::Map> map);
    ~GameScene() override; // 阶段9：unique_ptr 不完整类型需 cpp 内定义

    void OnLoad() override;
    void Update(float deltaTime) override;
    void Render(legend::render::Renderer& renderer, legend::render::Camera2D& camera) override;

    // 阶段8.1：快照恢复供 RAII RestoreGuard（SkillChecks.cpp 匿名命名空间）调用
    void RestoreSkillWorldSnapshot();

private:
    bool LoadPlayerCharacter();
    void UpdateCamera(float deltaTime);
    void ClampCameraToMap();
    // 自动化验收：LEGEND_AUTO_YSORT=1 时玩家自动走到树上方/下方验证遮挡切换
    void UpdateAutoYsortWalk(float deltaTime);

    // 自动验证（结果写入日志 PASS/FAIL）
    void RunCollisionVerification();
    void RunYSortVerification();
    void RunCollisionSourceVerification();
    void RunEditedMapCheck();
    void RunDirection8Check();
    void RunAnimationCheck();
    void RunSpriteSheetCheck();
    void RunAnimationDirectionFrameCheck();
    void RunCharacterTransformCheck();
    void RunCharacterRenderCheck();
    void ApplyAutoTestHooks();
    void LogMapStats(double deltaTime);
    // LEGEND_AUTO_DIRECTION_CYCLE=1：8 方向循环（每方向 Idle/Walk 各一段）
    void UpdateDirectionCycle();
    // 断言当前 Direction 与 Animation Clip 均为期望状态（在动画状态机更新后调用）
    void RunDirectionCycleAssertion(int dirIdx, bool walkPhase);
    // F2：脚底碰撞盒 + Feet 十字
    void DrawCharacterDebug(legend::render::SpriteBatch& batch);
    // 阶段12 指令四十/四十五：远程玩家 Debug 绘制（复用 Debug Quad，不做正式美术）
    void DrawRemotePlayers(legend::render::SpriteBatch& batch);
    // 阶段13 指令五十六/五十七：远程怪物 Debug 绘制（红/橙 Debug Quad，不做美术）
    void DrawRemoteMonsters(legend::render::SpriteBatch& batch);
    // 阶段20 指令十六/三十四：NPC Debug 绘制（Quad + 名字 + 任务 Marker !/?/灰点）。
    void DrawRemoteNpcs(legend::render::SpriteBatch& batch);
    // 阶段21 指令十八/一百一十四：Portal 发光门 Debug 绘制（名字走 F9 面板）。
    void DrawRemotePortals(legend::render::SpriteBatch& batch);

    // ---- 阶段4：World Actor System ----
    // 阶段4 静态自检：注册表 / 目标句柄 / 生成器（结果写日志）
    void RunActorRegistryCheck();
    void RunTargetHandleCheck();
    void RunSpawnerCheck(const legend::world::WorldSpawnStats& stats);
    // 阶段4.1 自检：动画真正逐帧推进 / monster.json 配置一致性 / 无效模板容错
    void RunAnimationRuntimeCheck();
    void RunMonsterConfigCheck();
    void RunMonsterTemplateFailureCheck();
    // F3：AI Debug 覆盖层（最近几只怪的 Aggro/Leash 圈、Home 十字、Wander 目标、目标连线）
    void DrawAIDebugOverlay(legend::render::SpriteBatch& batch);
    void DrawLine(legend::render::SpriteBatch& batch, const legend::math::Vector2& from,
                  const legend::math::Vector2& to, float thickness,
                  const legend::math::Color& color);
    void DrawCircle(legend::render::SpriteBatch& batch, const legend::math::Vector2& center,
                    float radius, const legend::math::Color& color);
    // LEGEND_AUTO_AI_TEST=1：AI 验收时间线（Aggro -> Leash -> Wander）
    void UpdateAITest(float deltaTime);

    // ---- 阶段5：Combat Core ----
    // 静态自检：属性/公式/冷却/攻击距离/动画事件/死亡
    void RunCombatStatsCheck();
    void RunCombatResolverCheck();
    void RunAttackCooldownCheck();
    void RunAttackRangeCheck();
    void RunAnimationEventCheck();
    void RunDeathCheck();
    // 阶段5.1 自检：Active 校验 / 配置跨字段校验 / 世界 AABB / 目标生命周期
    void RunCombatActiveCheck();
    void RunMonsterCombatConfigCheck();
    void RunCharacterHitTestCheck();
    void RunCombatTargetLifecycleCheck();
    // 阶段6 自检：经验/成长/物品库/背包堆叠/背包满/掉落Roll/拾取/部分拾取/死亡奖励
    void RunExperienceCheck();
    void RunLevelGrowthCheck();
    void RunItemDatabaseCheck();
    void RunInventoryStackCheck();
    void RunInventoryFullCheck();
    void RunLootRollCheck();
    void RunGroundLootPickupCheck();
    void RunPartialPickupCheck();
    void RunDeathRewardCheck();
    // 阶段6.1 自检：64位经验 / ItemDatabase 失败路径 / 死亡掉落真实集成（exactly-once）
    void RunExperience64Check();
    void RunItemDatabaseFailureCheck();
    void RunDeathLootIntegrationCheck();
    // 阶段7 自检：Equipment 定义/槽位/Instance 操作/装备卸下交换/满包事务/唯一实例/
    // 属性重算/HP clamp/等级+装备/装备掉落/装备比较
    void RunEquipmentDefinitionCheck();
    void RunEquipmentSlotCheck();
    void RunInventoryInstanceCheck();
    void RunEquipCheck();
    void RunUnequipCheck();
    void RunEquipmentSwapCheck();
    void RunFullInventoryUnequipCheck();
    void RunFullInventorySwapCheck();
    void RunEquipmentUniqueInstanceCheck();
    void RunEquipmentStatsCheck();
    void RunEquipmentHpClampCheck();
    void RunLevelEquipmentCheck();
    void RunEquipmentLootCheck();
    void RunEquipmentComparisonCheck();
    // 阶段7.1 自检：类型守卫 / 掉落配置 / 槽覆盖保护 / 正式掉落表
    void RunEquipmentTypeGuardCheck();
    void RunEquipmentLootConfigCheck();
    void RunSlotOverwriteGuardCheck();
    void RunOfficialEquipmentLootCheck();
    // LEGEND_AUTO_EQUIPMENT_TEST=1：阶段7 验收时间线（Kill->Loot->Pickup->Equip->Stats->Swap->Unequip）
    void UpdateEquipmentTest(float deltaTime);
    // 阶段7.2：统一恢复正式 slime 掉落表（所有退出路径：正常/FAIL/timeout 均在 Stage90 调用）
    void RestoreEquipmentTestLootOverride();
    // 阶段7.2 自检：Override 后按正式表逐项恢复（itemId/chance/min/max 全一致）
    void RunEquipmentTestRestoreCheck();
    // ---- 阶段8：Skill Core System 自检（26 个 Check，实现在 SkillChecks.cpp） ----
    void RunSkillDatabaseCheck();
    void RunSkillDatabaseFailureCheck();
    void RunSkillDefinitionValidationCheck();
    void RunSkillLoadoutCheck();
    void RunSkillManaCheck();
    void RunSkillCooldownCheck();
    void RunSkillCastValidationCheck();
    void RunSkillManaCooldownCheck();
    void RunSkillAnimationEventCheck();
    void RunSkillInterruptCheck();
    void RunSkillSingleTargetDamageCheck();
    void RunSkillDefenseCheck();
    void RunSkillRangeCheck();
    void RunSkillAOECheck();
    void RunSkillAOEDeathCheck();
    void RunSkillTargetDeathBeforeEventCheck();
    void RunSkillTargetDespawnCheck();
    void RunSkillCastStateCheck();
    void RunSkillMovementLockCheck();
    void RunSkillBasicAttackInteractionCheck();
    void RunEquipmentSkillDamageCheck();
    void RunSkillAttackSnapshotCheck();
    void RunSkillRespawnResetCheck();
    void RunSkillAggroCheck();
    void RunSkillDeathRewardCheck();
    void RunSkillAOERewardCheck();
    // LEGEND_AUTO_SKILL_TEST=1：阶段8 验收时间线（真实施法链路 0~11 阶段）
    void UpdateSkillTest(float deltaTime);
    // F7：Skill Debug 覆盖层（蓝色 Mana 条 + 4 技能槽 CD 比例方块）
    void DrawSkillDebugOverlay(legend::render::SpriteBatch& batch);
    // 窗口标题 Skill 段（" | MP: 85/100 | S1 ..."，LogMapStats 调用）
    std::string GetSkillStatusText() const;
    // F6：Equipment Debug 覆盖层（6 槽状态 + Base/Equipment/Final ATK/DEF/HP 几何显示）
    void DrawEquipmentDebugOverlay(legend::render::SpriteBatch& batch);
    // 窗口标题 Equipment 段（" | Equip: n/6"，LogMapStats 调用）
    std::string GetEquipmentStatusText() const;
    // LEGEND_AUTO_PROGRESSION_TEST=1：阶段6 验收时间线（击杀->Exp->Loot->拾取->升级->成长->Respawn）
    void UpdateProgressionTest(float deltaTime);
    // 阶段6：地上掉落 Debug 绘制（进 Y-Sort 队列后逐个绘制）
    void DrawGroundLoot(const legend::world::GroundLoot& loot);
    // F5：Progression/Loot Debug 覆盖层（最近掉落圈/拾取范围/距离连线）
    void DrawProgressionDebugOverlay(legend::render::SpriteBatch& batch);
    // LEGEND_AUTO_COMBAT_TEST=1：战斗验收时间线（选怪/连击/反击/死亡/重生/玩家复活）
    void UpdateCombatTest(float deltaTime);
    // Player 死亡后 Debug 复活：回出生点满血 Normal South
    void UpdatePlayerRespawn(float deltaTime);
    // F4：Combat Debug 覆盖层（目标圈/攻击距离圈/连线/最近3只怪血条与状态）
    void DrawCombatDebugOverlay(legend::render::SpriteBatch& batch);
    // Monster 头顶血条（受伤/选中/F4 时显示）
    void DrawMonsterHealthBars(legend::render::SpriteBatch& batch);
    // 选中目标红圈
    void DrawTargetRing(legend::render::SpriteBatch& batch);

    legend::world::PlayerCombatController m_playerCombat;
    legend::math::Vector2 m_playerSpawnPosition{0.0f, 0.0f};

    // ---- 阶段24：Visual Runtime（在线模式真实资源渲染；离线路径不受影响）----
    std::unique_ptr<legend::client::VisualRuntime> m_visualRuntime;
    bool m_visualHookWired = false;   // 事件钩子只挂一次
    bool m_visualCameraSnapped = false; // 进入世界后首帧相机 snap
    bool m_visualSmoke = false;       // LEGEND_CLIENT_VISUAL_SMOKE=1：15s 存活冒烟
    double m_visualSmokeElapsed = 0.0;
    float m_playerRespawnTimer = 0.0f; // 死亡后复活倒计时
    bool m_combatDebug = false;        // F4

    // LEGEND_AUTO_COMBAT_TEST 时间线状态
    bool m_autoCombatTest = false;
    int m_combatTestStage = 0;
    double m_combatTestElapsed = 0.0;
    double m_combatTestStageElapsed = 0.0;
    bool m_combatTestStageEntered = false;
    legend::entity::EntityId m_combatTestSlimeId = 0;
    float m_combatTestLastSlimeHp = 0.0f;
    bool m_combatTestSawDamage = false;
    bool m_combatTestSawCounter = false;
    int m_combatTestFailures = 0;
    bool m_combatTestSummaryDone = false;

    std::shared_ptr<legend::map::Map> m_map;
    legend::map::MapRenderer m_mapRenderer;
    std::unique_ptr<PlayerCharacter> m_player;
    std::unique_ptr<legend::client::ClientNetworkController> m_networkController; // 阶段9：懒构造
    PlayerController m_playerController;
    legend::entity::CharacterController m_characterController;
    legend::render::CharacterRenderer m_characterRenderer;
    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> m_playerClips;
    std::shared_ptr<legend::render::Texture> m_whiteTexture;

    static constexpr float kCameraSpeed = 450.0f; // world units / second

    bool m_cameraFollow = true;
    bool m_collisionDebug = false;
    bool m_characterDebug = false;

    double m_statsLogTimer = 0.0;
    double m_sceneElapsed = 0.0;
    bool m_chunkCheckDone = false;

    // 自动化测试钩子（环境变量触发，仅用于验收）
    bool m_autoWalk = false;
    bool m_autoYsort = false;
    bool m_autoDirCycle = false;
    int m_lastCycleSegment = -1;
    // [DirectionCycleCheck] 断言调度与统计
    bool m_cycleVerifyPending = false;
    int m_cycleDirIdx = 0;
    bool m_cycleWalk = false;
    int m_dirCycleChecks = 0;
    int m_dirCycleFailures = 0;
    bool m_dirCycleSummaryDone = false;
    float m_ysortTreeX = 0.0f;
    float m_ysortTreeY = 0.0f;

    // ---- 阶段4：世界角色 ----
    legend::world::WorldActorManager m_worldActors;
    bool m_aiDebug = false;      // F3
    int m_lastVisibleActors = 0; // 上一帧 Y-Sort 收集到的可见世界角色数

    // LEGEND_AUTO_AI_TEST=1 时间线状态
    bool m_aiTest = false;
    int m_aiTestStage = 0;            // 0=Aggro 1=Leash 2=Wander 3=汇总 4=完成
    double m_aiTestElapsed = 0.0;     // 总时长
    double m_aiTestStageElapsed = 0.0;
    bool m_aiTestStageEntered = false;
    legend::entity::EntityId m_aiTestMonsterId = 0; // Aggro 阶段锁定的怪物
    int m_aiTestFailures = 0;
    bool m_aiTestSummaryDone = false;
    std::vector<legend::math::Vector2> m_wanderBasePositions; // Wander 阶段位移基准
    int m_aiTestHop = 0;            // Leash 阶段牵引步数
    float m_aiTestHopTimer = 0.0f;
    bool m_aiTestLeashTriggered = false;

    // ---- 阶段6：Progression / Loot ----
    bool m_progressionDebug = false; // F5
    // LEGEND_AUTO_PROGRESSION_TEST=1 时间线状态
    bool m_progTest = false;
    int m_progTestStage = 0;
    double m_progTestElapsed = 0.0;
    double m_progTestStageElapsed = 0.0;
    bool m_progTestStageEntered = false;
    legend::entity::EntityId m_progTestSlimeId = 0;
    int m_progTestBaselineLevel = 0;
    int m_progTestBaselineExp = 0;
    long long m_progTestBaselineTotalExp = 0;
    float m_progTestBaselineMaxHp = 0.0f;
    float m_progTestBaselineAttack = 0.0f;
    float m_progTestBaselineDefense = 0.0f;
    int m_progTestBagBaseline = 0;
    std::uint64_t m_progTestLootBaseline = 0;
    std::size_t m_progTestGroundBaseline = 0; // 阶段6.1：真实掉落内容验证基线
    int m_progTestKills = 0;
    int m_progTestFailures = 0;
    bool m_progTestSummaryDone = false;
    int m_progTestPickupApplied = 0;

    // ---- 阶段7：Equipment ----
    bool m_equipmentDebug = false; // F6
    // LEGEND_AUTO_EQUIPMENT_TEST=1 时间线状态
    bool m_equipTest = false;
    int m_equipTestStage = 0;
    double m_equipTestElapsed = 0.0;
    double m_equipTestStageElapsed = 0.0;
    bool m_equipTestStageEntered = false;
    legend::entity::EntityId m_equipTestSlimeId = 0;
    legend::item::ItemInstanceId m_equipTestSwordA = 0; // wooden_sword instanceId
    legend::item::ItemInstanceId m_equipTestSwordB = 0; // iron_sword instanceId
    float m_equipTestAttackBase = 0.0f;
    int m_equipTestFailures = 0;
    bool m_equipTestSummaryDone = false;
    // 阶段7.2：正式 slime 掉落表保存（Stage0 首次进入时保存一次，Stage90 统一恢复）
    std::vector<legend::world::LootEntry> m_equipTestOriginalSlimeLoot;
    bool m_equipTestLootSaved = false;

    // ---- 阶段8：Skill Core ----
    legend::skill::PlayerSkillController m_playerSkill; // 1~4 按键 / 施法流程 / 事件路由
    bool m_skillDebug = false; // F7
    // LEGEND_AUTO_SKILL_TEST=1 时间线状态
    bool m_skillTest = false;
    int m_skillTestStage = 0;
    double m_skillTestElapsed = 0.0;
    double m_skillTestStageElapsed = 0.0;
    bool m_skillTestStageEntered = false;
    legend::entity::EntityId m_skillTestTargetId = 0;     // 单体阶段目标
    float m_skillTestTargetHpBefore = 0.0f;               // 施法前目标 HP（伤害断言）
    float m_skillTestDropBaseline = 0.0f;                 // Stage3 首次技能伤害（装备对照）
    float m_skillTestManaBaseline = 0.0f;                 // MP 断言基线
    int m_skillTestFailures = 0;
    bool m_skillTestSummaryDone = false;
    bool m_skillTestCastRequested = false;                // 当前阶段施法已请求（防重复）
    legend::item::ItemInstanceId m_skillTestSwordId = 0;  // Stage7 装备测试实例
    bool m_skillTestInterrupted = false;                  // Stage8 打断已施加
    int m_skillTestLastStage = -1;                        // 阶段切换检测（重置 stageElapsed）
    // 阶段8.1：Auto Skill Test slime 掉落保存（Stage0 保存一次，Stage90 统一恢复）
    std::vector<legend::world::LootEntry> m_skillTestOriginalSlimeLoot;
    bool m_skillTestLootSaved = false;
    // 阶段8.1：世界快照（Integration Check / Auto Test 的保存/恢复 + 隔离验证）
    legend::skill::SkillWorldSnapshot m_skillWorldSnapshot;
    // ---- 阶段8.1：LEGEND_RUN_SKILL_CHECKS=1（或 Auto Skill Test）时才执行 Integration Check
    bool m_skillChecks = false;
    // ---- 阶段19：Quest Debug（F8；F7 已被 Skill Debug 占用——指令五十允许调整）----
    bool m_questDebug = false;
    // 阶段21：F9 Map Debug Panel 开关（指令一百一十二）。
    bool m_mapDebug = false;
    // ---- 阶段8.1 新增 Check / 快照方法（实现在 SkillChecks.cpp） ----
    void CaptureSkillWorldSnapshot();
    void RunSkillWorldStateIsolationCheck();
    void RunSkillActiveCheck();
    void RunSkillNoFreeRewardCheck();
    void RunSkillDeterministicRewardCheck();
    // 阶段8.2：完整状态恢复验收（Progression/Base/Final/Aggro/AI/全量快照）
    void RunSkillProgressionRestoreCheck();
    void RunSkillAggroRestoreCheck();
    void RunSkillFullStateRestoreCheck();
    // 阶段8.1：Auto Skill Test 掉落恢复（Stage90 统一，禁止空表冒充）
    void RestoreSkillTestLootOverride();
    // 阶段8.3：Auto Skill Test 临时装备统一清理（幂等；只按 m_skillTestSwordId
    // 的 instanceId 删除，绝不按 definitionId；Equip 中直接 TakeEquipped 不要求背包空位）
    void CleanupSkillTestTemporaryEquipment();
    // 阶段8.3：临时装备清理验收
    void RunSkillTemporaryEquipmentCleanupCheck();
    void RunSkillEquipmentFailureCleanupCheck();
    // 阶段8.4：临时装备 Final Stats 重算验收（单次 + 100 次防漂移循环）
    void RunSkillTemporaryEquipmentStatsCheck();
};
