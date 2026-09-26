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
    // 分轴碰撞自检（结果写入日志 PASS/FAIL）
    void RunCollisionVerification();
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

    // 自动化测试钩子（环境变量触发，仅用于验收）
    bool m_autoWalk = false;
};
