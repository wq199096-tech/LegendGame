#pragma once

#include <memory>

#include "Engine/Scene/Scene.h"

namespace legend::render {
class Texture;
}

// 引擎 V0.1 验收测试场景：
// 3000x3000 测试世界 + 地面 + 多个测试物体 + 玩家（WASD 移动）
// 方向键移动摄像机 / 滚轮缩放 / F 切换摄像机跟随
class TestScene final : public legend::scene::Scene {
public:
    TestScene();

    void OnLoad() override;
    void Update(float deltaTime) override;

private:
    void CreateGround(std::shared_ptr<legend::render::Texture> groundTexture,
                      std::shared_ptr<legend::render::Texture> wallTexture);
    void CreateTestObjects(std::shared_ptr<legend::render::Texture> blockTexture,
                           std::shared_ptr<legend::render::Texture> pillarTexture);
    void CreatePlayer(std::shared_ptr<legend::render::Texture> playerTexture);
    // Camera 坐标定义自检：结果写入日志（PASS/FAIL）
    void RunCameraVerification();

    static constexpr float kWorldSize = 3000.0f;
    static constexpr float kPlayerSpeed = 200.0f;   // world units / second
    static constexpr float kCameraSpeed = 450.0f;   // world units / second

    legend::scene::GameObject* m_player = nullptr;
    bool m_cameraFollow = true;
};
