#include "Client/Source/TestScene.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Render/Texture.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SpriteRenderer.h"

TestScene::TestScene() : legend::scene::Scene("TestScene") {}

void TestScene::OnLoad() {
    auto& engine = legend::Engine::Get();
    auto& resources = engine.GetResources();

    auto groundTexture = resources.CreateCheckerTexture(
        "internal/ground", 256, 64,
        legend::math::Color::FromRGBA8(56, 118, 56),
        legend::math::Color::FromRGBA8(44, 98, 44));
    auto blockTexture = resources.CreateSolidTexture(
        "internal/block", 64, legend::math::Color::FromRGBA8(74, 118, 200));
    auto pillarTexture = resources.CreateCheckerTexture(
        "internal/pillar", 64, 16,
        legend::math::Color::FromRGBA8(168, 168, 178),
        legend::math::Color::FromRGBA8(96, 96, 108));
    auto playerTexture = resources.CreateCheckerTexture(
        "internal/player", 64, 16,
        legend::math::Color::FromRGBA8(226, 62, 54),
        legend::math::Color::FromRGBA8(250, 244, 244));
    auto wallTexture = resources.CreateSolidTexture(
        "internal/wall", 64, legend::math::Color::FromRGBA8(60, 52, 48));

    if (!groundTexture || !blockTexture || !pillarTexture || !playerTexture || !wallTexture) {
        LOG_ERROR("TestScene: failed to generate test textures.");
        return;
    }

    CreateGround(groundTexture, wallTexture);
    CreateTestObjects(blockTexture, pillarTexture);
    CreatePlayer(playerTexture);

    auto& camera = engine.GetCamera();
    camera.SetZoom(1.0f);
    if (m_player != nullptr) {
        camera.SetPosition(m_player->GetTransform().GetPosition());
    }

    LOG_INFO("TestScene ready. World size: " + std::to_string(kWorldSize) +
             " x " + std::to_string(kWorldSize));
}

void TestScene::CreateGround(std::shared_ptr<legend::render::Texture> groundTexture,
                             std::shared_ptr<legend::render::Texture> wallTexture) {
    (void)wallTexture;

    // 256x256 地砖铺满 3000x3000 世界
    constexpr int kTileSize = 256;
    const int tileCount = static_cast<int>(kWorldSize) / kTileSize;
    for (int j = 0; j < tileCount; ++j) {
        for (int i = 0; i < tileCount; ++i) {
            auto* object = CreateGameObject("Ground_" + std::to_string(i) + "_" + std::to_string(j));
            auto* sprite = object->AddComponent<legend::scene::SpriteRenderer>();
            sprite->SetTexture(groundTexture);
            sprite->SetRenderOrder(-100);
            object->GetTransform().SetPosition({i * kTileSize + kTileSize * 0.5f,
                                                j * kTileSize + kTileSize * 0.5f});
        }
    }

    // 世界边界墙（使用缩放的纯色纹理）
    const float halfWorld = kWorldSize * 0.5f;
    const float wallThickness = 32.0f;
    const float wallLength = kWorldSize;
    const float wallScaleLength = wallLength / 64.0f;
    const float wallScaleThickness = wallThickness / 64.0f;

    struct WallDef { float x; float y; float scaleX; float scaleY; };
    const WallDef walls[] = {
        {halfWorld, wallThickness * 0.5f, wallScaleLength, wallScaleThickness},          // 上
        {halfWorld, kWorldSize - wallThickness * 0.5f, wallScaleLength, wallScaleThickness}, // 下
        {wallThickness * 0.5f, halfWorld, wallScaleThickness, wallScaleLength},          // 左
        {kWorldSize - wallThickness * 0.5f, halfWorld, wallScaleThickness, wallScaleLength}, // 右
    };
    for (const WallDef& wall : walls) {
        auto* object = CreateGameObject("WorldWall");
        auto* sprite = object->AddComponent<legend::scene::SpriteRenderer>();
        sprite->SetTexture(wallTexture);
        sprite->SetRenderOrder(-90);
        object->GetTransform().SetPosition({wall.x, wall.y});
        object->GetTransform().SetScale({wall.scaleX, wall.scaleY});
    }
}

void TestScene::CreateTestObjects(std::shared_ptr<legend::render::Texture> blockTexture,
                                  std::shared_ptr<legend::render::Texture> pillarTexture) {
    const std::vector<legend::math::Vector2> positions = {
        {400.0f, 400.0f},   {1200.0f, 700.0f},  {2200.0f, 400.0f},
        {700.0f, 1800.0f},  {2300.0f, 2100.0f}, {1500.0f, 2500.0f},
        {2600.0f, 2800.0f}, {1000.0f, 1200.0f}, {2000.0f, 1700.0f},
        {300.0f, 2700.0f},
    };

    for (size_t i = 0; i < positions.size(); ++i) {
        auto* object = CreateGameObject("TestObject_" + std::to_string(i));
        auto* sprite = object->AddComponent<legend::scene::SpriteRenderer>();
        sprite->SetTexture(i % 2 == 0 ? blockTexture : pillarTexture);
        sprite->SetRenderOrder(0);
        object->GetTransform().SetPosition(positions[i]);
        if (i % 3 == 0) {
            object->GetTransform().SetScale(2.0f);
        }
    }
}

void TestScene::CreatePlayer(std::shared_ptr<legend::render::Texture> playerTexture) {
    m_player = CreateGameObject("Player");
    auto* sprite = m_player->AddComponent<legend::scene::SpriteRenderer>();
    sprite->SetTexture(playerTexture);
    sprite->SetRenderOrder(10);
    m_player->GetTransform().SetPosition({kWorldSize * 0.5f, kWorldSize * 0.5f});
}

void TestScene::Update(float deltaTime) {
    auto& engine = legend::Engine::Get();
    auto& input = engine.GetInput();
    auto& camera = engine.GetCamera();

    // ---- 玩家移动：WASD，方向归一化后乘速度与 DeltaTime ----
    if (m_player != nullptr) {
        auto& transform = m_player->GetTransform();
        const float inputX = (input.IsKeyDown(SDL_SCANCODE_D) ? 1.0f : 0.0f) -
                             (input.IsKeyDown(SDL_SCANCODE_A) ? 1.0f : 0.0f);
        const float inputY = (input.IsKeyDown(SDL_SCANCODE_S) ? 1.0f : 0.0f) -
                             (input.IsKeyDown(SDL_SCANCODE_W) ? 1.0f : 0.0f);
        legend::math::Vector2 direction(inputX, inputY);
        if (direction.LengthSq() > 0.0f) {
            direction = direction.Normalized();
            transform.Translate(direction * kPlayerSpeed * deltaTime);

            // 限制在测试世界内
            legend::math::Vector2 position = transform.GetPosition();
            constexpr float kMargin = 32.0f;
            position.x = std::clamp(position.x, kMargin, kWorldSize - kMargin);
            position.y = std::clamp(position.y, kMargin, kWorldSize - kMargin);
            transform.SetPosition(position);
        }
    }

    // ---- F 切换摄像机跟随 ----
    if (input.IsKeyPressed(SDL_SCANCODE_F)) {
        m_cameraFollow = !m_cameraFollow;
        LOG_INFO(m_cameraFollow ? "Camera follow: enabled (F)"
                                : "Camera follow: disabled (F)");
    }

    if (m_cameraFollow) {
        // 平滑跟随玩家
        if (m_player != nullptr) {
            const legend::math::Vector2 target = m_player->GetTransform().GetPosition();
            const float smoothing = 1.0f - std::exp(-10.0f * deltaTime);
            camera.SetPosition(camera.GetPosition() + (target - camera.GetPosition()) * smoothing);
        }
    } else {
        // 方向键自由移动摄像机
        const float inputX = (input.IsKeyDown(SDL_SCANCODE_RIGHT) ? 1.0f : 0.0f) -
                             (input.IsKeyDown(SDL_SCANCODE_LEFT) ? 1.0f : 0.0f);
        const float inputY = (input.IsKeyDown(SDL_SCANCODE_DOWN) ? 1.0f : 0.0f) -
                             (input.IsKeyDown(SDL_SCANCODE_UP) ? 1.0f : 0.0f);
        legend::math::Vector2 direction(inputX, inputY);
        if (direction.LengthSq() > 0.0f) {
            direction = direction.Normalized();
            camera.Move(direction * kCameraSpeed * deltaTime / camera.GetZoom());
        }

        // 鼠标滚轮缩放
        const float wheel = input.GetMouseWheelDelta();
        if (wheel != 0.0f) {
            camera.SetZoom(camera.GetZoom() * (wheel > 0.0f ? 1.1f : 1.0f / 1.1f));
        }
    }
}
