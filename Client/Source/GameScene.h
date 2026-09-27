#pragma once

#include <memory>
#include <vector>

#include "Client/Character/PlayerCharacter.h"
#include "Client/Character/PlayerController.h"
#include "Client/World/WorldActorManager.h"
#include "Engine/Entity/CharacterController.h"
#include "Engine/Entity/EntityId.h"
#include "Engine/Map/MapRenderer.h"
#include "Engine/Math/Color.h"
#include "Engine/Render/CharacterRenderer.h"
#include "Engine/Scene/Scene.h"

// 数据驱动游戏场景：Map（Tile/Object/Collision/Occlusion）+ Character Entity 体系。
// 管线：InputManager -> PlayerController -> CharacterController -> Character -> Map Collision -> Position
class GameScene final : public legend::scene::Scene {
public:
    explicit GameScene(std::shared_ptr<legend::map::Map> map);

    void OnLoad() override;
    void Update(float deltaTime) override;
    void Render(legend::render::Renderer& renderer, legend::render::Camera2D& camera) override;

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

    std::shared_ptr<legend::map::Map> m_map;
    legend::map::MapRenderer m_mapRenderer;
    std::unique_ptr<PlayerCharacter> m_player;
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
};
