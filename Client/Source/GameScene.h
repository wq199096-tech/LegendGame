#pragma once

#include <memory>

#include "Engine/Map/MapRenderer.h"
#include "Engine/Scene/Scene.h"

namespace legend::map {
class Map;
}

// 数据驱动游戏场景：加载 Map（Tile/Object/Collision/Occlusion），
// 玩家移动经过地图碰撞，渲染使用 Chunk 可视剔除 + SpriteBatch 批渲染 + Y-Sort。
class GameScene final : public legend::scene::Scene {
public:
    explicit GameScene(std::shared_ptr<legend::map::Map> map);

    void OnLoad() override;
    void Update(float deltaTime) override;
    void Render(legend::render::Renderer& renderer, legend::render::Camera2D& camera) override;

private:
    legend::scene::GameObject* CreatePlayer();
    // 玩家脚部碰撞盒（中心位于脚底）是否被阻挡
    bool IsFeetBoxBlocked(const legend::math::Vector2& center) const;
    void MovePlayerWithCollision(float deltaTime);
    void UpdateCamera(float deltaTime);
    void ClampCameraToMap();
    // 自动化验收：LEGEND_AUTO_YSORT=1 时玩家自动走到树上方/下方验证遮挡切换
    void UpdateAutoYsortWalk(float deltaTime);
    // 分轴碰撞自检（结果写入日志 PASS/FAIL）
    void RunCollisionVerification();
    // Y-Sort 遮挡关系自检
    void RunYSortVerification();
    // 碰撞来源分离自检（Terrain/Manual/Object）
    void RunCollisionSourceVerification();
    // 编辑器闭环检查：LEGEND_EXPECT_BLOCKED_TILE="x,y" 验证编辑后地图该格已阻挡
    void RunEditedMapCheck();
    void ApplyAutoTestHooks();
    void LogMapStats(double deltaTime);

    std::shared_ptr<legend::map::Map> m_map;
    legend::map::MapRenderer m_mapRenderer;
    legend::scene::GameObject* m_player = nullptr;
    std::shared_ptr<legend::render::Texture> m_playerTexture;

    static constexpr float kPlayerSpeed = 200.0f;    // world units / second
    static constexpr float kCameraSpeed = 450.0f;    // world units / second
    static constexpr float kPlayerFeetHalfWidth = 20.0f;
    static constexpr float kPlayerFeetHalfHeight = 12.0f;
    static constexpr float kPlayerFeetOffsetY = 20.0f; // 脚底碰撞盒中心相对精灵中心的偏移

    bool m_cameraFollow = true;
    bool m_collisionDebug = false;

    double m_statsLogTimer = 0.0;
    double m_sceneElapsed = 0.0;
    bool m_chunkCheckDone = false;

    // 自动化测试钩子（环境变量触发，仅用于验收）
    bool m_autoWalk = false;
    bool m_autoYsort = false;
    float m_ysortTreeX = 0.0f;
    float m_ysortTreeY = 0.0f;
};
