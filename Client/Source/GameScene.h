#pragma once

#include <memory>

#include "Client/Character/PlayerCharacter.h"
#include "Client/Character/PlayerController.h"
#include "Engine/Entity/CharacterController.h"
#include "Engine/Map/MapRenderer.h"
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
    // F2：脚底碰撞盒 + Feet 十字
    void DrawCharacterDebug(legend::render::SpriteBatch& batch);

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
    float m_ysortTreeX = 0.0f;
    float m_ysortTreeY = 0.0f;
};
