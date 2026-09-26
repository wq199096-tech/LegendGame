#include "Client/Source/GameScene.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Map/Map.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Render/Texture.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SpriteRenderer.h"

GameScene::GameScene(std::shared_ptr<legend::map::Map> map)
    : legend::scene::Scene("GameScene"), m_map(std::move(map)) {}

void GameScene::OnLoad() {
    auto& engine = legend::Engine::Get();
    auto& resources = engine.GetResources();
    auto& renderer = engine.GetRenderer();

    // 注册 Tile / 物件占位纹理（游戏与编辑器共用）
    m_mapRenderer.CreateDefaultPlaceholderTextures(resources);

    m_playerTexture = resources.CreateCheckerTexture(
        "internal/player", 64, 16,
        legend::math::Color::FromRGBA8(226, 62, 54),
        legend::math::Color::FromRGBA8(250, 244, 244));
    m_mapRenderer.RegisterObjectTexture("player", m_playerTexture);

    if (!m_mapRenderer.Initialize(renderer.GetSpriteShader())) {
        LOG_ERROR("GameScene: MapRenderer initialize failed.");
        return;
    }

    // 玩家出生点：从地图中心螺旋寻找可行走 Tile
    const int centerTileX = m_map->GetWidth() / 2;
    const int centerTileY = m_map->GetHeight() / 2;
    int spawnTileX = centerTileX;
    int spawnTileY = centerTileY;
    for (int radius = 0; radius < 20; ++radius) {
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; ++dy) {
            for (int dx = -radius; dx <= radius && !found; ++dx) {
                const int tx = centerTileX + dx;
                const int ty = centerTileY + dy;
                if (!m_map->GetCollision().IsBlocked(tx, ty)) {
                    spawnTileX = tx;
                    spawnTileY = ty;
                    found = true;
                }
            }
        }
        if (found) {
            break;
        }
    }

    m_player = CreatePlayer();
    const float spawnX = legend::map::TileToWorldCenter(spawnTileX, static_cast<float>(m_map->GetTileSize()));
    const float spawnY = legend::map::TileToWorldCenter(spawnTileY, static_cast<float>(m_map->GetTileSize()));
    m_player->GetTransform().SetPosition({spawnX, spawnY});

    auto& camera = engine.GetCamera();
    camera.SetZoom(1.0f);
    camera.SetPosition({spawnX, spawnY});

    ApplyAutoTestHooks();
    RunCollisionVerification();
    RunYSortVerification();
    RunCollisionSourceVerification();
    RunEditedMapCheck();

    LOG_INFO("GameScene ready. Map: '" + m_map->GetName() + "', player spawn tile: (" +
             std::to_string(spawnTileX) + "," + std::to_string(spawnTileY) + ").");
}

legend::scene::GameObject* GameScene::CreatePlayer() {
    m_player = CreateGameObject("Player");
    auto* sprite = m_player->AddComponent<legend::scene::SpriteRenderer>();
    sprite->SetTexture(m_playerTexture);
    sprite->SetRenderOrder(10);
    return m_player;
}

bool GameScene::IsFeetBoxBlocked(const legend::math::Vector2& center) const {
    // 脚底碰撞盒四角采样（中心位于精灵中心下方 kPlayerFeetOffsetY 处）
    const float boxX = center.x;
    const float boxY = center.y + kPlayerFeetOffsetY;
    const float left = boxX - kPlayerFeetHalfWidth;
    const float right = boxX + kPlayerFeetHalfWidth;
    const float top = boxY - kPlayerFeetHalfHeight;
    const float bottom = boxY + kPlayerFeetHalfHeight;
    return m_map->IsWorldBlocked(left, top) || m_map->IsWorldBlocked(right, top) ||
           m_map->IsWorldBlocked(left, bottom) || m_map->IsWorldBlocked(right, bottom);
}

void GameScene::MovePlayerWithCollision(float deltaTime) {
    if (m_player == nullptr || m_autoYsort) {
        return;
    }
    auto& input = legend::Engine::Get().GetInput();
    auto& transform = m_player->GetTransform();

    float inputX = (input.IsKeyDown(SDL_SCANCODE_D) ? 1.0f : 0.0f) -
                   (input.IsKeyDown(SDL_SCANCODE_A) ? 1.0f : 0.0f);
    float inputY = (input.IsKeyDown(SDL_SCANCODE_S) ? 1.0f : 0.0f) -
                   (input.IsKeyDown(SDL_SCANCODE_W) ? 1.0f : 0.0f);
    if (m_autoWalk) {
        inputX = 1.0f; // 自动化验收：模拟按住 D
    }

    legend::math::Vector2 direction(inputX, inputY);
    if (direction.LengthSq() <= 0.0f) {
        return;
    }
    direction = direction.Normalized();

    const float step = kPlayerSpeed * deltaTime;
    const legend::math::Vector2 position = transform.GetPosition();

    // 分轴碰撞：X/Y 独立尝试，斜向撞墙时沿墙滑动
    legend::math::Vector2 moved = position;
    const legend::math::Vector2 stepX(position.x + direction.x * step, position.y);
    if (!IsFeetBoxBlocked(stepX)) {
        moved.x = stepX.x;
    }
    const legend::math::Vector2 stepY(moved.x, position.y + direction.y * step);
    if (!IsFeetBoxBlocked(stepY)) {
        moved.y = stepY.y;
    }
    transform.SetPosition(moved);
}

void GameScene::ClampCameraToMap() {
    auto& engine = legend::Engine::Get();
    auto& camera = engine.GetCamera();
    int viewportW = 1;
    int viewportH = 1;
    engine.GetRenderer().QueryViewportSize(viewportW, viewportH);

    const float halfViewW = static_cast<float>(viewportW) * 0.5f / camera.GetZoom();
    const float halfViewH = static_cast<float>(viewportH) * 0.5f / camera.GetZoom();
    const float worldW = m_map->GetWorldWidth();
    const float worldH = m_map->GetWorldHeight();

    legend::math::Vector2 clamped = camera.GetPosition();
    // 地图比视口大：限制在地图内；否则固定在地图中心
    if (worldW > halfViewW * 2.0f) {
        clamped.x = std::clamp(clamped.x, halfViewW, worldW - halfViewW);
    } else {
        clamped.x = worldW * 0.5f;
    }
    if (worldH > halfViewH * 2.0f) {
        clamped.y = std::clamp(clamped.y, halfViewH, worldH - halfViewH);
    } else {
        clamped.y = worldH * 0.5f;
    }
    camera.SetPosition(clamped);
}

void GameScene::UpdateCamera(float deltaTime) {
    auto& engine = legend::Engine::Get();
    auto& input = engine.GetInput();
    auto& camera = engine.GetCamera();

    // F 切换跟随
    if (input.IsKeyPressed(SDL_SCANCODE_F)) {
        m_cameraFollow = !m_cameraFollow;
        LOG_INFO(m_cameraFollow ? "Camera follow: enabled (F)" : "Camera follow: disabled (F)");
    }

    // 鼠标滚轮缩放：无论是否跟随都生效
    const float wheel = input.GetMouseWheelDelta();
    if (wheel != 0.0f) {
        camera.SetZoom(camera.GetZoom() * (wheel > 0.0f ? 1.1f : 1.0f / 1.1f));
    }

    if (m_cameraFollow) {
        if (m_player != nullptr) {
            const legend::math::Vector2 target = m_player->GetTransform().GetPosition();
            const float smoothing = 1.0f - std::exp(-10.0f * deltaTime);
            camera.SetPosition(camera.GetPosition() + (target - camera.GetPosition()) * smoothing);
        }
    } else {
        // 方向键自由移动（仅关闭跟随时）
        const float inputX = (input.IsKeyDown(SDL_SCANCODE_RIGHT) ? 1.0f : 0.0f) -
                             (input.IsKeyDown(SDL_SCANCODE_LEFT) ? 1.0f : 0.0f);
        const float inputY = (input.IsKeyDown(SDL_SCANCODE_DOWN) ? 1.0f : 0.0f) -
                             (input.IsKeyDown(SDL_SCANCODE_UP) ? 1.0f : 0.0f);
        legend::math::Vector2 direction(inputX, inputY);
        if (direction.LengthSq() > 0.0f) {
            direction = direction.Normalized();
            camera.Move(direction * kCameraSpeed * deltaTime / camera.GetZoom());
        }
    }

    ClampCameraToMap();
}

void GameScene::Update(float deltaTime) {
    auto& engine = legend::Engine::Get();
    auto& input = engine.GetInput();
    m_sceneElapsed += deltaTime;

    // F1 切换碰撞可视化
    if (input.IsKeyPressed(SDL_SCANCODE_F1)) {
        m_collisionDebug = !m_collisionDebug;
        LOG_INFO(m_collisionDebug ? "Collision debug: enabled (F1)" : "Collision debug: disabled (F1)");
    }

    MovePlayerWithCollision(deltaTime);
    if (m_autoYsort) {
        UpdateAutoYsortWalk(deltaTime);
    }
    UpdateCamera(deltaTime);
    LogMapStats(deltaTime);
}

void GameScene::Render(legend::render::Renderer& renderer, legend::render::Camera2D& camera) {
    int viewportW = 1;
    int viewportH = 1;
    renderer.QueryViewportSize(viewportW, viewportH);

    m_mapRenderer.BeginFrame(camera, static_cast<float>(viewportW), static_cast<float>(viewportH));
    m_mapRenderer.RenderGround(*m_map);

    // Pass 1：非遮挡物件（地面装饰），按 renderOrder / bottomY 排序
    // Object Culling：AABB + 边距，视口外物件不参与排序和绘制
    std::vector<const legend::map::MapObject*> decorations;
    std::vector<const legend::map::MapObject*> occluders;
    int totalObjects = 0;
    int visibleObjects = 0;
    for (const auto& object : m_map->GetObjects().Objects()) {
        ++totalObjects;
        if (!m_mapRenderer.IsObjectVisible(object)) {
            continue;
        }
        ++visibleObjects;
        if (!m_map->GetOcclusion().IsOccluder(object.id)) {
            decorations.push_back(&object);
        } else {
            occluders.push_back(&object);
        }
    }
    m_mapRenderer.SetObjectCounts(visibleObjects, totalObjects);
    std::sort(decorations.begin(), decorations.end(), legend::map::MapRenderer::YSortCompare);
    for (const legend::map::MapObject* object : decorations) {
        m_mapRenderer.DrawMapObject(*object);
    }
    m_mapRenderer.Flush();

    // Pass 2：Y-Sort —— 遮挡物件与玩家按底部 Y 合并排序（树冠/建筑遮挡玩家）
    std::vector<const legend::map::MapObject*> ySortList = occluders;
    legend::map::MapObject playerProxy;
    if (m_player != nullptr) {
        playerProxy.id = 0;
        playerProxy.name = "Player";
        playerProxy.textureId = "player";
        playerProxy.x = m_player->GetTransform().GetPosition().x;
        playerProxy.y = m_player->GetTransform().GetPosition().y;
        playerProxy.width = 64.0f;
        playerProxy.height = 64.0f;
        playerProxy.renderOrder = 10; // 仅在 bottomY 相同时作平局判定，不破坏 Y-Sort
        playerProxy.sortLayer = 0;
        ySortList.push_back(&playerProxy);
    }
    std::sort(ySortList.begin(), ySortList.end(), legend::map::MapRenderer::YSortCompare);

    bool playerDrawn = false;
    for (const legend::map::MapObject* object : ySortList) {
        if (object->id == 0 && !playerDrawn) {
            // 先冲刷已排在前面的物件，再绘制玩家，保证遮挡顺序正确
            m_mapRenderer.Flush();
            renderer.DrawSprite(*m_playerTexture, {object->x, object->y},
                                legend::render::SpriteDrawParams{});
            playerDrawn = true;
        } else {
            m_mapRenderer.DrawMapObject(*object);
        }
    }
    m_mapRenderer.Flush();

    if (m_collisionDebug) {
        m_mapRenderer.RenderCollisionOverlay(*m_map);
    }
    m_mapRenderer.EndFrame();
}

void GameScene::RunCollisionVerification() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool expected, bool actual) {
        const bool pass = expected == actual;
        LOG_INFO("[CollisionCheck] " + name + ": expected " + (expected ? "blocked" : "walkable") +
                 ", got " + (actual ? "blocked" : "walkable") + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    // 找一个水域 Tile 与一个草地 Tile
    bool waterFound = false;
    bool grassFound = false;
    legend::map::TilePoint waterTile{};
    legend::map::TilePoint grassTile{};
    for (int ty = 0; ty < m_map->GetHeight() && (!waterFound || !grassFound); ++ty) {
        for (int tx = 0; tx < m_map->GetWidth() && (!waterFound || !grassFound); ++tx) {
            const uint16_t tile = m_map->GetGroundTile(tx, ty);
            if (tile == static_cast<uint16_t>(legend::map::TileId::Water) && !waterFound) {
                waterTile = {tx, ty};
                waterFound = true;
            }
            if (tile == static_cast<uint16_t>(legend::map::TileId::Grass) &&
                !m_map->IsTileBlocked(tx, ty) && !grassFound) {
                grassTile = {tx, ty};
                grassFound = true;
            }
        }
    }

    const float ts = static_cast<float>(m_map->GetTileSize());
    if (waterFound) {
        check("water tile blocked", true,
              m_map->IsWorldBlocked(legend::map::TileToWorldCenter(waterTile.x, ts),
                                    legend::map::TileToWorldCenter(waterTile.y, ts)));
    }
    if (grassFound) {
        check("grass tile walkable", false,
              m_map->IsWorldBlocked(legend::map::TileToWorldCenter(grassTile.x, ts),
                                    legend::map::TileToWorldCenter(grassTile.y, ts)));
    }
    // 地图外视为阻挡
    check("out-of-bounds blocked", true, m_map->IsWorldBlocked(-9999.0f, -9999.0f));
    check("out-of-bounds blocked (2)", true,
          m_map->IsWorldBlocked(m_map->GetWorldWidth() + 9999.0f, 0.0f));

    LOG_INFO("[CollisionCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::ApplyAutoTestHooks() {
    const char* autoWalk = SDL_getenv("LEGEND_AUTO_WALK");
    if (autoWalk != nullptr && autoWalk[0] == '1') {
        m_autoWalk = true;
        LOG_INFO("Auto-test: walking right enabled (LEGEND_AUTO_WALK=1).");
    }
    const char* collisionDebug = SDL_getenv("LEGEND_AUTO_COLLISION");
    if (collisionDebug != nullptr && collisionDebug[0] == '1') {
        m_collisionDebug = true;
        LOG_INFO("Auto-test: collision debug overlay enabled (LEGEND_AUTO_COLLISION=1).");
    }
    const char* autoYsort = SDL_getenv("LEGEND_AUTO_YSORT");
    if (autoYsort != nullptr && autoYsort[0] == '1') {
        // 找第一棵参与 Y-Sort 的树，玩家在其上方/下方之间切换（配合引擎 2.5s/5.0s 两次截图）
        for (const auto& object : m_map->GetObjects().Objects()) {
            if (object.occluder && object.textureId == "tree") {
                m_autoYsort = true;
                m_ysortTreeX = object.x;
                m_ysortTreeY = object.y;
                break;
            }
        }
        if (m_autoYsort) {
            LOG_INFO("Auto-test: Y-Sort walk enabled (LEGEND_AUTO_YSORT=1), tree at (" +
                     std::to_string(m_ysortTreeX) + "," + std::to_string(m_ysortTreeY) + ").");
        }
    }
}

void GameScene::UpdateAutoYsortWalk(float deltaTime) {
    (void)deltaTime;
    if (m_player == nullptr) {
        return;
    }
    // 与引擎截图时刻对齐（LEGEND_AUTO_SHOT_TIMES="2.5,5.0"）：
    // 2.5s 前玩家在树上方（bottomY < 树 -> 玩家被树冠遮挡）；之后在树下方（玩家在前）。
    const float targetY = (m_sceneElapsed < 2.5) ? (m_ysortTreeY - 70.0f) : (m_ysortTreeY + 70.0f);
    m_player->GetTransform().SetPosition({m_ysortTreeX, targetY});
}

void GameScene::RunYSortVerification() {
    int failures = 0;

    // 找一棵树与一栋建筑（均需参与 Y-Sort 遮挡）
    const legend::map::MapObject* tree = nullptr;
    const legend::map::MapObject* building = nullptr;
    for (const auto& object : m_map->GetObjects().Objects()) {
        if (!object.occluder) {
            continue;
        }
        if (tree == nullptr && object.textureId == "tree") {
            tree = &object;
        }
        if (building == nullptr && object.textureId == "building") {
            building = &object;
        }
    }
    if (tree == nullptr || building == nullptr) {
        LOG_WARN("[YSortCheck] skipped: missing occluder tree/building in map.");
        return;
    }

    // 玩家代理：renderOrder = 10（旧的错误实现会因此永远压过 renderOrder=0 的遮挡物）
    legend::map::MapObject proxy;
    proxy.id = 0;
    proxy.sortLayer = 0;
    proxy.renderOrder = 10;
    proxy.width = 64.0f;
    proxy.height = 64.0f;

    auto behindCheck = [&](const char* name, const legend::map::MapObject* occluder,
                           float playerBottomY) {
        proxy.x = occluder->x;
        proxy.y = playerBottomY - 32.0f;
        std::vector<const legend::map::MapObject*> list = {occluder, &proxy};
        std::sort(list.begin(), list.end(), legend::map::MapRenderer::YSortCompare);
        // 玩家 bottomY 更小 -> 玩家先画（被遮挡物遮挡） -> 列表中玩家在前
        const bool pass = (list.front() == &proxy);
        LOG_INFO(std::string("[YSortCheck] ") + name + ": player bottomY=" +
                 std::to_string(playerBottomY) + ", occluder bottomY=" +
                 std::to_string(occluder->GetBottomY()) + " -> player drawn first: " +
                 (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    auto frontCheck = [&](const char* name, const legend::map::MapObject* occluder,
                          float playerBottomY) {
        proxy.x = occluder->x;
        proxy.y = playerBottomY - 32.0f;
        std::vector<const legend::map::MapObject*> list = {occluder, &proxy};
        std::sort(list.begin(), list.end(), legend::map::MapRenderer::YSortCompare);
        // 玩家 bottomY 更大 -> 玩家后画（在遮挡物前面）
        const bool pass = (list.back() == &proxy);
        LOG_INFO(std::string("[YSortCheck] ") + name + ": player bottomY=" +
                 std::to_string(playerBottomY) + ", occluder bottomY=" +
                 std::to_string(occluder->GetBottomY()) + " -> player drawn last: " +
                 (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    // 树：上方（被遮挡）/下方（在前）
    behindCheck("player behind tree", tree, tree->GetBottomY() - 100.0f);
    frontCheck("player in front of tree", tree, tree->GetBottomY() + 100.0f);
    // 建筑：上方 / 下方
    behindCheck("player behind building", building, building->GetBottomY() - 100.0f);
    frontCheck("player in front of building", building, building->GetBottomY() + 100.0f);

    LOG_INFO("[YSortCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunCollisionSourceVerification() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool expected, bool actual,
                             uint8_t flags, uint8_t expectedFlags) {
        const bool pass = expected == actual && flags == expectedFlags;
        LOG_INFO("[CollisionFinalCheck] " + name + ": blocked=" +
                 (actual ? std::string("true") : std::string("false")) +
                 ", flags=" + std::to_string(flags) + " (expected " +
                 std::to_string(expectedFlags) + ")" + " -> " +
                 ((expected == actual) ? (pass ? "PASS" : "FLAGS-FAIL")
                                       : std::string("FAIL")));
        if (!pass) {
            ++failures;
        }
    };
    constexpr uint8_t kTerrain = legend::map::CollisionSourceTerrain;
    constexpr uint8_t kManual = legend::map::CollisionSourceManual;
    constexpr uint8_t kObject = legend::map::CollisionSourceObject;

    // 找一个可用测试 Tile（草地且当前无任何阻挡来源）与一个 Water Tile
    legend::map::TilePoint freeTile{};
    bool freeFound = false;
    legend::map::TilePoint waterTile{};
    bool waterFound = false;
    for (int ty = 1; ty < m_map->GetHeight() - 1 && (!freeFound || !waterFound); ++ty) {
        for (int tx = 1; tx < m_map->GetWidth() - 1 && (!freeFound || !waterFound); ++tx) {
            if (!freeFound && m_map->GetGroundTile(tx, ty) == static_cast<uint16_t>(legend::map::TileId::Grass) &&
                !m_map->IsTileBlocked(tx, ty)) {
                freeTile = {tx, ty};
                freeFound = true;
            }
            if (!waterFound && m_map->GetGroundTile(tx, ty) == static_cast<uint16_t>(legend::map::TileId::Water)) {
                waterTile = {tx, ty};
                waterFound = true;
            }
        }
    }
    if (!freeFound || !waterFound) {
        LOG_WARN("[CollisionFinalCheck] skipped: missing test tiles.");
        return;
    }
    const float ts = static_cast<float>(m_map->GetTileSize());
    const float cx = legend::map::TileToWorldCenter(freeTile.x, ts);
    const float cy = legend::map::TileToWorldCenter(freeTile.y, ts);
    const float wx = legend::map::TileToWorldCenter(waterTile.x, ts);
    const float wy = legend::map::TileToWorldCenter(waterTile.y, ts);

    // ---- 1. Water + Manual=false + Object=0 => blocked (Terrain) ----
    check("1 water terrain blocked", true, m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), kTerrain);

    // ---- 2. Grass + Manual=true + Object=0 => blocked (Manual) ----
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, true);
    check("2 grass manual blocked", true, m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), kManual);

    // ---- 3. Grass + Manual=false + Object>0 => blocked (Object) ----
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, false);
    legend::map::MapObject rock;
    rock.id = m_map->GetObjects().GetMaxObjectId() + 100001;
    rock.name = "test_final_rock";
    rock.textureId = "rock";
    rock.width = 48.0f;
    rock.height = 48.0f;
    rock.x = cx;
    rock.y = cy;
    rock.blocking = true;
    m_map->SpawnObject(rock);
    check("3 grass object blocked", true, m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), kObject);

    // ---- 4. Water + Manual=true + Object>0 => blocked (Terrain|Manual|Object = 7) ----
    legend::map::MapObject building;
    building.id = m_map->GetObjects().GetMaxObjectId() + 100001;
    building.name = "test_final_building";
    building.textureId = "building";
    building.width = 192.0f;
    building.height = 128.0f;
    building.x = wx;
    building.y = wy;
    building.blocking = true;
    m_map->SpawnObject(building);
    m_map->GetCollision().SetBlocked(waterTile.x, waterTile.y, true);
    check("4 water+manual+object blocked", true, m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y),
          kTerrain | kManual | kObject);

    // ---- 5. Water 改 Grass，Manual=true => 仍然 blocked (Manual|Object，Terrain 消失) ----
    m_map->SetGroundTile(waterTile.x, waterTile.y, static_cast<uint16_t>(legend::map::TileId::Grass));
    check("5 water->grass manual kept blocked", true,
          m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), kManual | kObject);

    // ---- 6. Water 改 Grass 后 Manual=false + Object=0 => walkable ----
    m_map->GetCollision().SetBlocked(waterTile.x, waterTile.y, false);
    m_map->DespawnObject(building.id);
    check("6 grass cleared => walkable", false, m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), 0);

    // ---- 7. Object 删除，Manual=true => 仍 blocked ----
    m_map->DespawnObject(rock.id);
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, true);
    check("7 object deleted, manual kept blocked", true,
          m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), kManual);

    // ---- 8. Object 删除，Manual=false，Terrain=Grass => walkable ----
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, false);
    check("8 all sources cleared => walkable", false,
          m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), 0);

    LOG_INFO("[CollisionFinalCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunEditedMapCheck() {
    // 编辑器闭环验证：编辑器刷的 Water（Terrain 来源）在客户端加载后必须仍阻挡
    const char* expectTile = SDL_getenv("LEGEND_EXPECT_BLOCKED_TILE");
    if (expectTile == nullptr || expectTile[0] == '\0') {
        return;
    }
    int tx = -1;
    int ty = -1;
    if (std::sscanf(expectTile, "%d,%d", &tx, &ty) != 2) {
        LOG_WARN("[EditedMapCheck] invalid LEGEND_EXPECT_BLOCKED_TILE value.");
        return;
    }
    const float ts = static_cast<float>(m_map->GetTileSize());
    const bool blocked = m_map->IsTileBlocked(tx, ty);
    LOG_INFO(std::string("[EditedMapCheck] tile (") + std::to_string(tx) + "," +
             std::to_string(ty) + ") expected blocked, got " +
             (blocked ? "blocked -> PASS" : "walkable -> FAIL"));
}

void GameScene::LogMapStats(double deltaTime) {
    m_statsLogTimer += deltaTime;
    if (m_statsLogTimer < 2.0) {
        return;
    }
    m_statsLogTimer = 0.0;

    const auto& stats = m_mapRenderer.GetLastFrameStats();
    const std::string status =
        "Map: " + m_map->GetName() +
        " | Chunks: " + std::to_string(stats.visibleChunks) +
        " | Tiles: " + std::to_string(stats.renderedTiles) +
        " | Objects: " + std::to_string(stats.visibleObjects) + "/" + std::to_string(stats.totalObjects) +
        " | DC: " + std::to_string(stats.drawCalls);
    legend::Engine::Get().SetStatusText(status);
    LOG_INFO("[MapStats] " + status);

    // [ChunkCheck] 一次性断言：Chunk visible 状态与统计一致（每帧重置生效）
    if (!m_chunkCheckDone && stats.visibleChunks > 0) {
        m_chunkCheckDone = true;
        int counted = 0;
        for (int cy = 0; cy < m_map->GetChunkCountY(); ++cy) {
            for (int cx = 0; cx < m_map->GetChunkCountX(); ++cx) {
                const legend::map::MapChunk* chunk = m_map->GetChunk(cx, cy);
                if (chunk != nullptr && chunk->IsVisible()) {
                    ++counted;
                }
            }
        }
        const bool pass = counted == stats.visibleChunks;
        LOG_INFO("[ChunkCheck] visible-consistency: counted " + std::to_string(counted) +
                 ", stats " + std::to_string(stats.visibleChunks) + " -> " +
                 (pass ? "PASS" : "FAIL"));
    }
}
