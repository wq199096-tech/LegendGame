#include "Client/Source/GameScene.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Animation/AnimationStateMachine.h"
#include "Client/World/MonsterCharacter.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Entity/EntityIdAllocator.h"
#include "Engine/Map/Map.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Render/SpriteBatch.h"
#include "Engine/Render/Texture.h"

GameScene::GameScene(std::shared_ptr<legend::map::Map> map)
    : legend::scene::Scene("GameScene"), m_map(std::move(map)) {}

void GameScene::OnLoad() {
    auto& engine = legend::Engine::Get();
    auto& resources = engine.GetResources();
    auto& renderer = engine.GetRenderer();

    m_mapRenderer.CreateDefaultPlaceholderTextures(resources);
    if (!m_mapRenderer.Initialize(renderer.GetSpriteShader())) {
        LOG_ERROR("GameScene: MapRenderer initialize failed.");
        return;
    }

    m_whiteTexture = resources.CreateSolidTexture(
        "internal/white", 64, legend::math::Color(1.0f, 1.0f, 1.0f, 1.0f));

    if (!LoadPlayerCharacter()) {
        LOG_ERROR("GameScene: player character setup failed.");
        return;
    }

    // 玩家出生点：从地图中心螺旋寻找可行走 Tile（按角色 footprint 判定）
    legend::entity::CharacterController controller;
    const int centerTileX = m_map->GetWidth() / 2;
    const int centerTileY = m_map->GetHeight() / 2;
    int spawnTileX = centerTileX;
    int spawnTileY = centerTileY;
    const float ts = static_cast<float>(m_map->GetTileSize());
    for (int radius = 0; radius < 24; ++radius) {
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; ++dy) {
            for (int dx = -radius; dx <= radius && !found; ++dx) {
                const int tx = centerTileX + dx;
                const int ty = centerTileY + dy;
                const legend::math::Vector2 feet(
                    legend::map::TileToWorldCenter(tx, ts),
                    legend::map::TileToWorldCenter(ty, ts));
                if (!controller.IsPositionBlocked(*m_map, m_player->GetFootprint(), feet)) {
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
    m_player->SetPosition({legend::map::TileToWorldCenter(spawnTileX, ts),
                           legend::map::TileToWorldCenter(spawnTileY, ts)});

    // ---- 阶段4：世界角色（NPC + Monster + AI） ----
    if (!m_worldActors.Initialize(resources)) {
        LOG_ERROR("GameScene: world actor manager initialize failed.");
        return; // 世界角色初始化失败不能静默继续
    }
    m_worldActors.RegisterPlayer(m_player.get());
    const int npcSpawned = m_worldActors.SpawnNPCs(*m_map);
    const legend::world::WorldSpawnStats monsterStats = m_worldActors.SpawnMonsters(*m_map);

    auto& camera = engine.GetCamera();
    camera.SetZoom(1.0f);
    camera.SetPosition(m_player->GetPosition()); // 跟随脚底位置

    ApplyAutoTestHooks();
    RunDirection8Check();
    RunAnimationCheck();
    RunSpriteSheetCheck();
    RunAnimationDirectionFrameCheck();
    RunCharacterTransformCheck();
    RunCharacterRenderCheck();
    RunCollisionVerification();
    RunYSortVerification();
    RunCollisionSourceVerification();
    RunEditedMapCheck();
    RunActorRegistryCheck();
    RunTargetHandleCheck();
    RunSpawnerCheck(monsterStats);
    RunMonsterConfigCheck();
    RunMonsterTemplateFailureCheck();
    RunAnimationRuntimeCheck();

    LOG_INFO("GameScene ready. Map: '" + m_map->GetName() + "', player spawn tile: (" +
             std::to_string(spawnTileX) + "," + std::to_string(spawnTileY) + "), NPCs: " +
             std::to_string(npcSpawned) + ", monsters: " + std::to_string(monsterStats.spawned) +
             "/" + std::to_string(monsterStats.requested) + ".");
}

bool GameScene::LoadPlayerCharacter() {
    auto& resources = legend::Engine::Get().GetResources();
    const std::string assetsRoot = resources.GetAssetRoot();

    legend::animation::CharacterDefinition definition;
    const bool loaded = legend::animation::LoadCharacterDefinition(
        assetsRoot + "/Characters/TestHero/character.json", definition);

    if (loaded) {
        // animations 路径相对 Assets 根，与 spriteSheet 纹理同规则
        auto clipsMap = legend::animation::LoadAnimationClips(assetsRoot + "/" + definition.animationsPath);
        LOG_INFO("Player clips loaded: " + std::to_string(clipsMap.size()) + " from " +
                 definition.animationsPath);
        auto sheet = legend::animation::LoadSpriteSheet(definition, legend::Engine::Get().GetResources());
        if (sheet && !clipsMap.empty()) {
            m_playerClips = std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>(
                std::move(clipsMap));
            m_player = std::make_unique<PlayerCharacter>(legend::entity::EntityIdAllocator::Next(),
                                                         definition, m_playerClips, sheet);
            LOG_INFO("Player character created from assets: '" + definition.name + "'.");
            return true;
        }
        LOG_ERROR("Character asset load failed (sprite sheet / animations). Using debug fallback.");
    }

    // ---- Fallback：程序生成 Debug Character（不崩溃） ----
    legend::animation::CharacterDefinition fallback;
    fallback.name = "DebugHero";
    fallback.frameWidth = 64;
    fallback.frameHeight = 64;
    fallback.visualWidth = 64.0f;
    fallback.visualHeight = 64.0f;
    fallback.moveSpeed = 200.0f;
    auto texture = resources.CreateSolidTexture(
        "character/fallback", 64, legend::math::Color::FromRGBA8(90, 160, 220));
    auto sheet = std::make_shared<legend::animation::SpriteSheet>();
    if (!sheet->Initialize(texture, 64, 64)) {
        return false;
    }
    std::unordered_map<std::string, legend::animation::AnimationClip> clips;
    legend::animation::AnimationClip idle;
    idle.name = "idle_south";
    idle.loop = true;
    idle.frames.push_back({0, 1.0f});
    clips[idle.name] = idle;
    m_playerClips = std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>(
        std::move(clips));
    m_player = std::make_unique<PlayerCharacter>(legend::entity::EntityIdAllocator::Next(),
                                                 fallback, m_playerClips, sheet);
    LOG_WARN("Using fallback debug character.");
    return true;
}

void GameScene::UpdateCamera(float deltaTime) {
    auto& engine = legend::Engine::Get();
    auto& input = engine.GetInput();
    auto& camera = engine.GetCamera();

    // F 切换跟随（跟随 Character Feet Position）
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
        if (m_player) {
            const legend::math::Vector2 target = m_player->GetPosition();
            const float smoothing = 1.0f - std::exp(-10.0f * deltaTime);
            camera.SetPosition(camera.GetPosition() + (target - camera.GetPosition()) * smoothing);
        }
    } else {
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

void GameScene::Update(float deltaTime) {
    auto& engine = legend::Engine::Get();
    auto& input = engine.GetInput();
    m_sceneElapsed += deltaTime;

    // F1 切换碰撞可视化 / F2 切换角色 Debug
    if (input.IsKeyPressed(SDL_SCANCODE_F1)) {
        m_collisionDebug = !m_collisionDebug;
        LOG_INFO(m_collisionDebug ? "Collision debug: enabled (F1)" : "Collision debug: disabled (F1)");
    }
    if (input.IsKeyPressed(SDL_SCANCODE_F2)) {
        m_characterDebug = !m_characterDebug;
        LOG_INFO(m_characterDebug ? "Character debug: enabled (F2)" : "Character debug: disabled (F2)");
    }
    if (input.IsKeyPressed(SDL_SCANCODE_F3)) {
        m_aiDebug = !m_aiDebug;
        LOG_INFO(m_aiDebug ? "AI debug: enabled (F3)" : "AI debug: disabled (F3)");
    }

    // 输入 -> 控制器 -> 角色 -> 地图碰撞 -> 位置
    if (m_autoDirCycle) {
        UpdateDirectionCycle();
        m_playerController.Update(input, m_characterController, *m_player, *m_map, deltaTime);
    } else if (m_autoYsort) {
        UpdateAutoYsortWalk(deltaTime);
    } else {
        if (m_autoWalk) {
            m_playerController.SetVirtualInput({1.0f, 0.0f});
        } else {
            m_playerController.SetVirtualInput({0.0f, 0.0f});
        }
        m_playerController.Update(input, m_characterController, *m_player, *m_map, deltaTime);
    }

    // 动画状态机 + 帧推进：Idle/Walk + 8 方向，真正逐帧播放
    m_player->UpdateAnimation(deltaTime);

    // ---- 世界角色统一更新：NPC 动画 / Monster AI + 动画 ----
    m_worldActors.Update(*m_map, deltaTime);

    // 断言时机：动画状态机已按最终 direction/moving 选好 Clip
    if (m_autoDirCycle && m_cycleVerifyPending) {
        m_cycleVerifyPending = false;
        RunDirectionCycleAssertion(m_cycleDirIdx, m_cycleWalk);
    }

    // LEGEND_AUTO_AI_TEST=1：AI 验收时间线
    if (m_aiTest) {
        UpdateAITest(deltaTime);
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

    // ---- 统一 Y-Sort 队列：MapObject + 世界角色（Player/NPC/Monster） ----
    std::vector<legend::map::RenderSortItem> items;
    int totalObjects = 0;
    int visibleObjects = 0;
    for (const auto& object : m_map->GetObjects().Objects()) {
        ++totalObjects;
        if (!m_mapRenderer.IsObjectVisible(object)) {
            continue;
        }
        ++visibleObjects;
        items.push_back({0, object.GetBottomY(), object.renderOrder,
                         legend::map::RenderSortItem::Type::MapObject, &object, nullptr});
    }

    // 世界角色统一收集（视口剔除在 WorldActorManager 内完成）
    m_worldActors.CollectRenderItems(items, m_mapRenderer.GetViewLeft(),
                                     m_mapRenderer.GetViewRight(), m_mapRenderer.GetViewTop(),
                                     m_mapRenderer.GetViewBottom());
    m_lastVisibleActors = 0;
    for (const auto& item : items) {
        if (item.type == legend::map::RenderSortItem::Type::Character) {
            ++m_lastVisibleActors;
        }
    }
    m_mapRenderer.SetObjectCounts(visibleObjects, totalObjects);

    std::sort(items.begin(), items.end(), legend::map::RenderSortItem::Compare);

    for (const auto& item : items) {
        if (item.type == legend::map::RenderSortItem::Type::MapObject) {
            m_mapRenderer.DrawMapObject(*item.mapObject);
        } else if (item.character != nullptr) {
            m_mapRenderer.Flush(); // 冲刷排在前面的物件，保证遮挡顺序
            m_characterRenderer.Draw(m_mapRenderer.GetBatch(), *item.character);
        }
    }
    m_mapRenderer.Flush();

    if (m_collisionDebug) {
        m_mapRenderer.RenderCollisionOverlay(*m_map);
    }
    if (m_characterDebug) {
        DrawCharacterDebug(m_mapRenderer.GetBatch());
    }
    if (m_aiDebug) {
        DrawAIDebugOverlay(m_mapRenderer.GetBatch());
    }
    m_mapRenderer.EndFrame();
}

void GameScene::DrawCharacterDebug(legend::render::SpriteBatch& batch) {
    if (!m_player || !m_whiteTexture) {
        return;
    }
    const auto& fp = m_player->GetFootprint();
    const legend::math::Vector2& feet = m_player->GetPosition();
    const legend::math::Vector2 fpCenter(feet.x + fp.offsetX, feet.y + fp.offsetY);

    // 脚底碰撞区域：半透明黄色矩形
    batch.DrawQuad(*m_whiteTexture, fpCenter, {fp.width / 64.0f, fp.height / 64.0f}, 0.0f,
                   legend::math::Color(1.0f, 1.0f, 0.0f, 0.35f));
    // Feet Point：白色十字
    batch.DrawQuad(*m_whiteTexture, feet, {16.0f / 64.0f, 2.0f / 64.0f}, 0.0f,
                   legend::math::Color(1.0f, 1.0f, 1.0f, 0.9f));
    batch.DrawQuad(*m_whiteTexture, feet, {2.0f / 64.0f, 16.0f / 64.0f}, 0.0f,
                   legend::math::Color(1.0f, 1.0f, 1.0f, 0.9f));
}

void GameScene::ApplyAutoTestHooks() {
    const char* autoWalk = SDL_getenv("LEGEND_AUTO_WALK");
    if (autoWalk != nullptr && autoWalk[0] == '1') {
        m_autoWalk = true;
        LOG_INFO("Auto-test: walking right enabled (LEGEND_AUTO_WALK=1).");
    }
    const char* dirCycle = SDL_getenv("LEGEND_AUTO_DIRECTION_CYCLE");
    if (dirCycle != nullptr && dirCycle[0] == '1') {
        m_autoDirCycle = true;
        LOG_INFO("Auto-test: 8-direction cycle enabled (LEGEND_AUTO_DIRECTION_CYCLE=1), "
                 "1.2s per direction (0.6s idle + 0.6s walk).");
    }
    const char* aiTest = SDL_getenv("LEGEND_AUTO_AI_TEST");
    if (aiTest != nullptr && aiTest[0] == '1') {
        m_aiTest = true;
        LOG_INFO("Auto-test: AI acceptance timeline enabled (LEGEND_AUTO_AI_TEST=1), "
                 "stages: Aggro -> Leash -> Wander.");
    }
    const char* collisionDebug = SDL_getenv("LEGEND_AUTO_COLLISION");
    if (collisionDebug != nullptr && collisionDebug[0] == '1') {
        m_collisionDebug = true;
        LOG_INFO("Auto-test: collision debug overlay enabled (LEGEND_AUTO_COLLISION=1).");
    }
    const char* autoYsort = SDL_getenv("LEGEND_AUTO_YSORT");
    if (autoYsort != nullptr && autoYsort[0] == '1') {
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
    if (!m_player) {
        return;
    }
    // 与引擎截图时刻对齐（LEGEND_AUTO_SHOT_TIMES="2.5,5.0"）：
    // 2.5s 前玩家在树上方（bottomY < 树 -> 被树冠遮挡）；之后在树下方（玩家在前）。
    const float targetY = (m_sceneElapsed < 2.5) ? (m_ysortTreeY - 70.0f) : (m_ysortTreeY + 70.0f);
    m_player->SetPosition({m_ysortTreeX, targetY});
}

void GameScene::RunDirection8Check() {
    int failures = 0;
    struct Case {
        float x;
        float y;
        legend::entity::Direction8 expected;
        const char* name;
    };
    const Case cases[] = {
        {0, -1, legend::entity::Direction8::North, "North"},
        {1, -1, legend::entity::Direction8::NorthEast, "NorthEast"},
        {1, 0, legend::entity::Direction8::East, "East"},
        {1, 1, legend::entity::Direction8::SouthEast, "SouthEast"},
        {0, 1, legend::entity::Direction8::South, "South"},
        {-1, 1, legend::entity::Direction8::SouthWest, "SouthWest"},
        {-1, 0, legend::entity::Direction8::West, "West"},
        {-1, -1, legend::entity::Direction8::NorthWest, "NorthWest"},
    };
    for (const Case& c : cases) {
        const auto got = legend::entity::DirectionFromVector({c.x, c.y}, legend::entity::Direction8::South);
        const bool pass = got == c.expected;
        LOG_INFO(std::string("[Direction8Check] (") + std::to_string(c.x) + "," +
                 std::to_string(c.y) + ") expected " + c.name + " -> " +
                 (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    }
    // 零向量保留 fallback
    const auto kept = legend::entity::DirectionFromVector({0, 0}, legend::entity::Direction8::NorthEast);
    const bool keepPass = kept == legend::entity::Direction8::NorthEast;
    LOG_INFO(std::string("[Direction8Check] zero vector keeps fallback -> ") +
             (keepPass ? "PASS" : "FAIL"));
    if (!keepPass) {
        ++failures;
    }
    LOG_INFO("[Direction8Check] completed, failures = " + std::to_string(failures));
}

void GameScene::RunAnimationCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[AnimationCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    // 8 方向 x Idle/Walk = 16 clips 全部存在
    bool allPresent = true;
    const char* dirNames[] = {"south", "southwest", "west", "northwest",
                              "north", "northeast", "east", "southeast"};
    for (const char* dir : dirNames) {
        for (const char* state : {"idle_", "walk_"}) {
            if (m_playerClips->find(state + std::string(dir)) == m_playerClips->end()) {
                allPresent = false;
                LOG_WARN("[AnimationCheck] missing clip: " + std::string(state) + dir);
            }
        }
    }
    check("16 direction clips present", allPresent);

    if (m_playerClips->empty() || !m_player) {
        LOG_INFO("[AnimationCheck] completed, failures = " + std::to_string(failures));
        return;
    }

    auto& player = m_player->GetAnimationPlayer();
    // Play 同名 Clip 不重置进度
    player.Play("walk_east");
    player.Update(0.05f);
    const int ordinalBefore = player.GetCurrentFrameOrdinal();
    player.Play("walk_east");
    check("re-play same clip does not reset", player.GetCurrentFrameOrdinal() == ordinalBefore);

    // Frame 随 deltaTime 前进（0.25s = 前进约 2 帧，未到 Loop 回卷点）
    player.Update(0.25f); // 单帧 0.12s
    check("frame advances with deltaTime", player.GetCurrentFrameOrdinal() != ordinalBefore);

    // Loop 正常回卷
    const int totalFrames = player.GetCurrentFrameCount();
    player.Update(0.12f * totalFrames * 2.0f + 0.02f);
    check("loop wraps back into range",
          player.GetCurrentFrameOrdinal() >= 0 && player.GetCurrentFrameOrdinal() < totalFrames);

    LOG_INFO("[AnimationCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunCharacterTransformCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[CharacterTransformCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    if (!m_player) {
        LOG_INFO("[CharacterTransformCheck] completed, failures = " + std::to_string(failures));
        return;
    }

    const auto& fp = m_player->GetFootprint();
    const auto& visual = m_player->GetVisual();

    // ---- Before：记录相对 Feet 的真实偏移 ----
    const legend::math::Vector2 feetBefore = m_player->GetPosition();
    const legend::math::Vector2 fpCenterBefore(feetBefore.x + fp.offsetX, feetBefore.y + fp.offsetY);
    const legend::math::Vector2 spriteCenterBefore =
        legend::render::CharacterRenderer::GetSpriteDrawCenter(*m_player);
    const legend::math::Vector2 fpOffsetBefore = fpCenterBefore - feetBefore;
    const legend::math::Vector2 spOffsetBefore = spriteCenterBefore - feetBefore;

    // ---- 移动角色 ----
    m_player->SetPosition(feetBefore + legend::math::Vector2(500.0f, 300.0f));

    // ---- After：重新计算相对 Feet 的偏移 ----
    const legend::math::Vector2 feetAfter = m_player->GetPosition();
    const legend::math::Vector2 fpCenterAfter(feetAfter.x + fp.offsetX, feetAfter.y + fp.offsetY);
    const legend::math::Vector2 spriteCenterAfter =
        legend::render::CharacterRenderer::GetSpriteDrawCenter(*m_player);
    const legend::math::Vector2 fpOffsetAfter = fpCenterAfter - feetAfter;
    const legend::math::Vector2 spOffsetAfter = spriteCenterAfter - feetAfter;

    // 1. 移动前后两组偏移必须一致
    const bool fpConstant = std::fabs(fpOffsetAfter.x - fpOffsetBefore.x) < 0.01f &&
                            std::fabs(fpOffsetAfter.y - fpOffsetBefore.y) < 0.01f;
    check("footprint offset constant after move", fpConstant);

    const bool spConstant = std::fabs(spOffsetAfter.x - spOffsetBefore.x) < 0.01f &&
                            std::fabs(spOffsetAfter.y - spOffsetBefore.y) < 0.01f;
    check("sprite pivot offset constant after move", spConstant);

    // 2. 非平凡断言：偏移等于理论值
    //    Footprint Center = Feet + (offsetX, offsetY)
    const bool fpTheoretical = std::fabs(fpOffsetBefore.x - fp.offsetX) < 0.01f &&
                               std::fabs(fpOffsetBefore.y - fp.offsetY) < 0.01f;
    check("footprint center = feet + footprint offset", fpTheoretical);

    //    Sprite Center = Feet + visual * (0.5 - pivot)
    const float expectedSpX = visual.width * (0.5f - visual.pivot.x);
    const float expectedSpY = visual.height * (0.5f - visual.pivot.y);
    const bool spTheoretical = std::fabs(spOffsetBefore.x - expectedSpX) < 0.01f &&
                               std::fabs(spOffsetBefore.y - expectedSpY) < 0.01f;
    check("sprite center = feet + pivot offset", spTheoretical);

    // 3. Feet Position 不等于 Sprite Center（pivot 产生可见偏移）
    check("feet != sprite center",
          std::fabs(spOffsetBefore.y) > 0.01f || std::fabs(spOffsetBefore.x) > 0.01f);

    // 还原到原位
    m_player->SetPosition(feetBefore);
    LOG_INFO("[CharacterTransformCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunSpriteSheetCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[SpriteSheetCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    const auto& sheet = m_player->GetSpriteSheet();
    if (!sheet || !sheet->IsValid()) {
        LOG_ERROR("[SpriteSheetCheck] sprite sheet invalid.");
        return;
    }
    check("Columns: 6", sheet->GetColumns() == 6);
    check("Rows: 8", sheet->GetRows() == 8);
    check("Frames: 48", sheet->GetFrameCount() == 48);
    LOG_INFO("[SpriteSheetCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunAnimationDirectionFrameCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[AnimationDirectionFrameCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    static const char* kDirNames[8] = {"south", "southwest", "west", "northwest",
                                       "north", "northeast", "east", "southeast"};

    // 每方向 Idle/Walk Clip 引用的帧必须全部落在对应方向行 [row*6, row*6+5]；
    // 两个方向不得使用同一行（rowMask 位图互斥检查）
    for (int row = 0; row < 8; ++row) {
        int rowMask = 0;
        bool clipsFound = true;
        for (const char* prefix : {"idle_", "walk_"}) {
            const auto it = m_playerClips->find(std::string(prefix) + kDirNames[row]);
            if (it == m_playerClips->end() || it->second.frames.empty()) {
                check(std::string(prefix) + kDirNames[row] + " exists", false);
                clipsFound = false;
                continue;
            }
            for (const auto& frame : it->second.frames) {
                const int frameRow = frame.frameIndex / 6;
                rowMask |= 1 << frameRow;
                if (frameRow != row) {
                    check(std::string(prefix) + kDirNames[row] + " frame " +
                              std::to_string(frame.frameIndex) + " in row " +
                              std::to_string(row),
                          false);
                }
            }
        }
        if (clipsFound) {
            check(std::string(kDirNames[row]) + " uses only row " + std::to_string(row) +
                      " (no shared rows)",
                  rowMask == (1 << row));
        }
    }

    LOG_INFO("[AnimationDirectionFrameCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunCharacterRenderCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[CharacterRenderCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    const auto& sheet = m_player->GetSpriteSheet();
    if (!sheet || !sheet->IsValid()) {
        LOG_ERROR("[CharacterRenderCheck] sprite sheet invalid.");
        return;
    }
    const auto& visual = m_player->GetVisual();
    const float texW = static_cast<float>(sheet->GetTexture().GetWidth());
    const float texH = static_cast<float>(sheet->GetTexture().GetHeight());
    const legend::math::Vector2 scale = legend::render::CharacterRenderer::GetSpriteScale(*m_player);
    const float quadW = texW * scale.x;
    const float quadH = texH * scale.y;

    // 整张图集 576x768，但角色 Quad 世界尺寸必须恒等于 visualWidth x visualHeight（96x96）
    check("quad width == visualWidth (96)", std::fabs(quadW - visual.width) < 0.01f);
    check("quad height == visualHeight (96)", std::fabs(quadH - visual.height) < 0.01f);
    check("atlas texture is 576x768", std::fabs(texW - 576.0f) < 0.01f && std::fabs(texH - 768.0f) < 0.01f);
    check("draw size != atlas size", quadW < texW && quadH < texH);

    LOG_INFO("[CharacterRenderCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::UpdateDirectionCycle() {
    if (!m_player) {
        return;
    }
    // 每方向 1.2s：0.6s Idle + 0.6s Walk，按枚举序 South->SW->W->NW->N->NE->E->SE
    constexpr double kSegmentLength = 1.2;
    const int segment = static_cast<int>(m_sceneElapsed / kSegmentLength);
    const int dirIdx = segment % 8;
    const double phase = m_sceneElapsed - segment * kSegmentLength;
    const bool walkPhase = phase >= 0.6;
    const auto targetDirection = static_cast<legend::entity::Direction8>(dirIdx);

    static const legend::math::Vector2 kDirVectors[8] = {
        {0.0f, 1.0f},  { -1.0f, 1.0f }, { -1.0f, 0.0f }, { -1.0f, -1.0f },
        {0.0f, -1.0f}, {1.0f, -1.0f},  {1.0f, 0.0f},   {1.0f, 1.0f},
    };

    if (walkPhase) {
        // Walk：对应方向向量走正常 PlayerController / CharacterController 移动管线
        m_playerController.SetVirtualInput(kDirVectors[dirIdx]);
    } else {
        // Idle：零输入会保留上一方向，必须显式设置目标方向 -> Idle + 当前目标 Direction
        m_player->SetDirection(targetDirection);
        m_playerController.SetVirtualInput(legend::math::Vector2{0.0f, 0.0f});
    }

    const int stateKey = segment * 2 + (walkPhase ? 1 : 0);
    if (stateKey != m_lastCycleSegment) {
        m_lastCycleSegment = stateKey;
        static const char* kNames[8] = {"South", "SouthWest", "West", "NorthWest",
                                        "North", "NorthEast", "East", "SouthEast"};
        LOG_INFO(std::string("[DirectionCycle] ") + kNames[dirIdx] + (walkPhase ? " WALK" : " IDLE") +
                 " (virtual input " + std::to_string(walkPhase ? kDirVectors[dirIdx].x : 0.0f) + "," +
                 std::to_string(walkPhase ? kDirVectors[dirIdx].y : 0.0f) + ")");
        // 断言挂起：动画状态机更新后执行（Clip 在本帧稍后才会切换）
        m_cycleVerifyPending = true;
        m_cycleDirIdx = dirIdx;
        m_cycleWalk = walkPhase;
    }
}

void GameScene::RunDirectionCycleAssertion(int dirIdx, bool walkPhase) {
    static const char* kNames[8] = {"South", "SouthWest", "West", "NorthWest",
                                    "North", "NorthEast", "East", "SouthEast"};
    const auto expectedDir = static_cast<legend::entity::Direction8>(dirIdx);
    const std::string expectedClip =
        std::string(walkPhase ? "walk_" : "idle_") + legend::entity::Direction8Name(expectedDir);

    // 同时验证 Direction 变量与 Animation Clip 名称
    const bool dirOK = m_player->GetDirection() == expectedDir;
    const std::string actualClip = m_player->GetAnimationPlayer().GetCurrentClipName();
    const bool clipOK = actualClip == expectedClip;
    const bool pass = dirOK && clipOK;

    LOG_INFO(std::string("[DirectionCycleCheck] ") + kNames[dirIdx] +
             (walkPhase ? " Walk" : " Idle") + " -> expected '" + expectedClip + "', got '" +
             actualClip + "' (direction " + (dirOK ? "ok" : "MISMATCH") + ") -> " +
             (pass ? "PASS" : "FAIL"));
    ++m_dirCycleChecks;
    if (!pass) {
        ++m_dirCycleFailures;
    }
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
    check("out-of-bounds blocked", true, m_map->IsWorldBlocked(-9999.0f, -9999.0f));
    check("out-of-bounds blocked (2)", true,
          m_map->IsWorldBlocked(m_map->GetWorldWidth() + 9999.0f, 0.0f));

    LOG_INFO("[CollisionCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunYSortVerification() {
    int failures = 0;

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

    legend::map::RenderSortItem characterItem;
    characterItem.type = legend::map::RenderSortItem::Type::Character;
    characterItem.sortLayer = 0;
    characterItem.renderOrder = 10; // 旧实现会因此永远压过 renderOrder=0 的遮挡物

    auto runCase = [&](const char* name, const legend::map::MapObject* occluder,
                       float playerBottomY, bool expectPlayerFirst) {
        characterItem.sortY = playerBottomY;
        std::vector<legend::map::RenderSortItem> list = {
            {0, occluder->GetBottomY(), occluder->renderOrder,
             legend::map::RenderSortItem::Type::MapObject, occluder, nullptr},
            characterItem};
        std::sort(list.begin(), list.end(), legend::map::RenderSortItem::Compare);
        const bool playerFirst = (list.front().type == legend::map::RenderSortItem::Type::Character);
        const bool pass = playerFirst == expectPlayerFirst;
        LOG_INFO(std::string("[YSortCheck] ") + name + ": player bottomY=" +
                 std::to_string(playerBottomY) + ", occluder bottomY=" +
                 std::to_string(occluder->GetBottomY()) + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    runCase("player behind tree", tree, tree->GetBottomY() - 100.0f, true);
    runCase("player in front of tree", tree, tree->GetBottomY() + 100.0f, false);
    runCase("player behind building", building, building->GetBottomY() - 100.0f, true);
    runCase("player in front of building", building, building->GetBottomY() + 100.0f, false);

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

    // 1. Water + Manual=false + Object=0 => blocked (Terrain)
    check("1 water terrain blocked", true, m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), kTerrain);

    // 2. Grass + Manual=true + Object=0 => blocked (Manual)
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, true);
    check("2 grass manual blocked", true, m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), kManual);

    // 3. Grass + Manual=false + Object>0 => blocked (Object)
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

    // 4. Water + Manual=true + Object>0 => blocked (7)
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
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), kTerrain | kManual | kObject);

    // 5. Water 改 Grass，Manual=true => 仍然 blocked
    m_map->SetGroundTile(waterTile.x, waterTile.y, static_cast<uint16_t>(legend::map::TileId::Grass));
    check("5 water->grass manual kept blocked", true,
          m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), kManual | kObject);

    // 6. Manual=false + Object=0 => walkable
    m_map->GetCollision().SetBlocked(waterTile.x, waterTile.y, false);
    m_map->DespawnObject(building.id);
    check("6 grass cleared => walkable", false, m_map->IsTileBlocked(waterTile.x, waterTile.y),
          m_map->GetCollisionFlags(waterTile.x, waterTile.y), 0);

    // 7. Object 删除，Manual=true => 仍 blocked
    m_map->DespawnObject(rock.id);
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, true);
    check("7 object deleted, manual kept blocked", true,
          m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), kManual);

    // 8. 全清 => walkable
    m_map->GetCollision().SetBlocked(freeTile.x, freeTile.y, false);
    check("8 all sources cleared => walkable", false,
          m_map->IsTileBlocked(freeTile.x, freeTile.y),
          m_map->GetCollisionFlags(freeTile.x, freeTile.y), 0);

    LOG_INFO("[CollisionFinalCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunEditedMapCheck() {
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
    std::string status =
        "Map: " + m_map->GetName() +
        " | Chunks: " + std::to_string(stats.visibleChunks) +
        " | Tiles: " + std::to_string(stats.renderedTiles) +
        " | Objects: " + std::to_string(stats.visibleObjects) + "/" + std::to_string(stats.totalObjects) +
        " | DC: " + std::to_string(stats.drawCalls) +
        // 阶段4：世界角色统计
        " | Actors: " + std::to_string(m_worldActors.GetRegistry().Count()) +
        " | Visible: " + std::to_string(m_lastVisibleActors) +
        " | AI: " + std::to_string(m_worldActors.GetMonsterCount()) +
        " | Scans: " + std::to_string(m_worldActors.GetTotalScanCount());

    // F2：Entity / Direction / State / Clip / Frame
    if (m_characterDebug && m_player) {
        const auto& player = m_player->GetAnimationPlayer();
        status += " | Entity: " + m_player->GetName() +
                  " | Dir: " + legend::entity::Direction8Name(m_player->GetDirection()) +
                  " | Clip: " + player.GetCurrentClipName() +
                  " | Frame: " + std::to_string(player.GetCurrentFrameOrdinal() + 1) + "/" +
                  std::to_string(player.GetCurrentFrameCount());
        LOG_INFO("[CharacterDebug] Entity: " + m_player->GetName() +
                 " | Direction: " + legend::entity::Direction8Name(m_player->GetDirection()) +
                 " | Clip: " + player.GetCurrentClipName() +
                 " | Frame: " + std::to_string(player.GetCurrentFrameOrdinal() + 1) + "/" +
                 std::to_string(player.GetCurrentFrameCount()) +
                 " | Feet: (" + std::to_string(m_player->GetPosition().x) + "," +
                 std::to_string(m_player->GetPosition().y) + ")");
    }

    // F3：AI Debug（最近 6 只怪：状态/目标/距离）
    if (m_aiDebug) {
        const legend::math::Vector2 playerPos =
            m_player ? m_player->GetPosition() : legend::math::Vector2{0, 0};
        auto monsters = m_worldActors.GetMonsters();
        std::sort(monsters.begin(), monsters.end(),
                  [&playerPos](const legend::world::MonsterCharacter* a,
                               const legend::world::MonsterCharacter* b) {
                      const legend::math::Vector2 da = a->GetPosition() - playerPos;
                      const legend::math::Vector2 db = b->GetPosition() - playerPos;
                      return da.LengthSq() < db.LengthSq();
                  });
        const int count = static_cast<int>(std::min<size_t>(monsters.size(), 6));
        for (int i = 0; i < count; ++i) {
            const legend::world::MonsterCharacter* monster = monsters[i];
            const legend::math::Vector2 delta = monster->GetPosition() - playerPos;
            LOG_INFO("[AIDebug] " + monster->GetName() + "#" +
                     std::to_string(monster->GetId()) + " state=" +
                     legend::world::MonsterAIStateName(monster->GetAIState()) +
                     " target=" + (monster->GetTargetHandle().IsEmpty()
                                       ? std::string("none")
                                       : std::to_string(monster->GetTargetHandle().GetId())) +
                     " dist=" + std::to_string(delta.Length()));
        }
    }

    legend::Engine::Get().SetStatusText(status);
    LOG_INFO("[MapStats] " + status);

    // [DirectionCycleCheck] 汇总：16 种状态（8 方向 x Idle/Walk）全部跑完后输出一次
    if (m_autoDirCycle && !m_dirCycleSummaryDone && m_dirCycleChecks >= 16) {
        m_dirCycleSummaryDone = true;
        LOG_INFO("[DirectionCycleCheck] completed, checks = " + std::to_string(m_dirCycleChecks) +
                 ", failures = " + std::to_string(m_dirCycleFailures));
    }

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

// ==================== 阶段4：World Actor System ====================

void GameScene::RunActorRegistryCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[ActorRegistryCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    const auto& registry = m_worldActors.GetRegistry();
    const legend::entity::EntityId playerId = m_player ? m_player->GetId() : 0;

    // 1. Player 已注册
    check("player registered", playerId != 0 && registry.Get(playerId) != nullptr);
    // 2. NPC 数量 = 3
    const auto npcs = registry.GetByType(legend::entity::ActorType::NPC);
    check("NPC count == 3", npcs.size() == 3);
    // 3. Monster 数量 = 17
    const auto monsters = registry.GetByType(legend::entity::ActorType::Monster);
    check("Monster count == 17", monsters.size() == 17);
    // 4. FindInRadius 命中 Player
    bool playerFound = false;
    if (m_player) {
        for (const legend::entity::Character* actor :
             registry.FindInRadius(m_player->GetPosition(), 100.0f)) {
            if (actor->GetId() == playerId) {
                playerFound = true;
                break;
            }
        }
    }
    check("FindInRadius contains player", playerFound);

    // 5. 注册/注销/去重语义（临时对象，不入渲染统计——visible=false）
    auto& mutableRegistry = m_worldActors.GetRegistry();
    auto emptyClips = std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>();
    const auto tempId = legend::entity::EntityIdAllocator::Next();
    auto temp = std::make_unique<legend::entity::Character>(
        tempId, "RegistryCheckTemp", legend::entity::ActorType::Monster, 0.0f,
        legend::entity::CharacterFootprint{}, legend::entity::CharacterVisual{}, emptyClips);
    temp->SetVisible(false);
    const std::size_t before = mutableRegistry.Count();
    mutableRegistry.Register(temp.get());
    mutableRegistry.Register(temp.get()); // 重复注册必须被忽略
    check("duplicate register ignored", mutableRegistry.Count() == before + 1);
    check("Get(temp) found", mutableRegistry.Get(tempId) != nullptr);
    mutableRegistry.Unregister(tempId);
    check("Unregister removes actor", mutableRegistry.Get(tempId) == nullptr);

    // 6. Active 语义：inactive Actor 不进入 GetByType/FindInRadius/TargetHandle
    const auto inactiveId = legend::entity::EntityIdAllocator::Next();
    auto inactive = std::make_unique<legend::entity::Character>(
        inactiveId, "RegistryCheckInactive", legend::entity::ActorType::Monster, 0.0f,
        legend::entity::CharacterFootprint{}, legend::entity::CharacterVisual{}, emptyClips);
    inactive->SetActive(false);
    if (m_player) {
        inactive->SetPosition(m_player->GetPosition()); // 与 Player 同点，必进 FindInRadius 圆
    }
    mutableRegistry.Register(inactive.get());
    // 行为明确：Get(id) 按登记返回（含 inactive）；过滤由 GetByType/FindInRadius/TargetHandle 负责
    check("Get(id) returns registered inactive actor by id",
          mutableRegistry.Get(inactiveId) == inactive.get());
    bool inactiveInTypeQuery = false;
    for (const legend::entity::Character* actor :
         mutableRegistry.GetByType(legend::entity::ActorType::Monster)) {
        if (actor->GetId() == inactiveId) {
            inactiveInTypeQuery = true;
            break;
        }
    }
    check("GetByType excludes inactive", !inactiveInTypeQuery);
    bool inactiveInRadius = false;
    if (m_player) {
        for (const legend::entity::Character* actor :
             mutableRegistry.FindInRadius(m_player->GetPosition(), 100.0f)) {
            if (actor->GetId() == inactiveId) {
                inactiveInRadius = true;
                break;
            }
        }
    }
    check("FindInRadius excludes inactive", !inactiveInRadius);
    legend::entity::TargetHandle inactiveHandle;
    inactiveHandle.Set(inactiveId);
    check("TargetHandle invalid for inactive target",
          !inactiveHandle.IsValid(registry) && inactiveHandle.Resolve(registry) == nullptr);
    inactiveHandle.Clear();
    mutableRegistry.Unregister(inactiveId);
    check("Unregister removes inactive actor", mutableRegistry.Get(inactiveId) == nullptr);

    LOG_INFO("[ActorRegistryCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunTargetHandleCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[TargetHandleCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    const auto& registry = m_worldActors.GetRegistry();
    legend::entity::TargetHandle handle;

    // 1. 空句柄
    check("empty handle invalid", handle.IsEmpty() && !handle.IsValid(registry) &&
                                       handle.Resolve(registry) == nullptr);
    // 2. 指向 Player
    if (m_player) {
        handle.Set(m_player->GetId());
        check("resolve player", handle.IsValid(registry) &&
                                    handle.Resolve(registry) == m_player.get());
    }
    // 3. 目标注销后安全失效
    auto& mutableRegistry = m_worldActors.GetRegistry();
    auto emptyClips = std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>();
    const auto tempId = legend::entity::EntityIdAllocator::Next();
    auto temp = std::make_unique<legend::entity::Character>(
        tempId, "TargetCheckTemp", legend::entity::ActorType::Monster, 0.0f,
        legend::entity::CharacterFootprint{}, legend::entity::CharacterVisual{}, emptyClips);
    mutableRegistry.Register(temp.get());
    handle.Set(tempId);
    check("resolve temp actor", handle.IsValid(registry) &&
                                    handle.Resolve(registry) == temp.get());
    mutableRegistry.Unregister(tempId);
    check("unregistered target invalidated",
          !handle.IsValid(registry) && handle.Resolve(registry) == nullptr);
    // 4. Clear
    handle.Clear();
    check("cleared handle empty", handle.IsEmpty());

    LOG_INFO("[TargetHandleCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunSpawnerCheck(const legend::world::WorldSpawnStats& stats) {
    const bool pass = stats.requested > 0 && stats.spawned == stats.requested &&
                      stats.failed == 0;
    LOG_INFO("[SpawnerCheck] Requested: " + std::to_string(stats.requested) +
             " Spawned: " + std::to_string(stats.spawned) +
             " Failed: " + std::to_string(stats.failed) + " -> " +
             (pass ? "PASS" : "FAIL"));
}

void GameScene::DrawLine(legend::render::SpriteBatch& batch, const legend::math::Vector2& from,
                         const legend::math::Vector2& to, float thickness,
                         const legend::math::Color& color) {
    if (!m_whiteTexture) {
        return;
    }
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.5f) {
        return;
    }
    const float angleDeg = std::atan2(dy, dx) * 180.0f / 3.14159265f;
    batch.DrawQuad(*m_whiteTexture, {(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f},
                   {length / 64.0f, thickness / 64.0f}, angleDeg, color);
}

void GameScene::DrawCircle(legend::render::SpriteBatch& batch,
                           const legend::math::Vector2& center, float radius,
                           const legend::math::Color& color) {
    constexpr int kSegments = 32;
    const float step = 6.28318530f / static_cast<float>(kSegments);
    legend::math::Vector2 prev(center.x + radius, center.y);
    for (int i = 1; i <= kSegments; ++i) {
        const float angle = step * static_cast<float>(i);
        const legend::math::Vector2 point(center.x + std::cos(angle) * radius,
                                          center.y + std::sin(angle) * radius);
        DrawLine(batch, prev, point, 3.0f, color);
        prev = point;
    }
}

void GameScene::DrawAIDebugOverlay(legend::render::SpriteBatch& batch) {
    if (!m_player || !m_whiteTexture) {
        return;
    }
    const legend::math::Vector2 playerPos = m_player->GetPosition();
    auto monsters = m_worldActors.GetMonsters();
    std::sort(monsters.begin(), monsters.end(),
              [&playerPos](const legend::world::MonsterCharacter* a,
                           const legend::world::MonsterCharacter* b) {
                  const legend::math::Vector2 da = a->GetPosition() - playerPos;
                  const legend::math::Vector2 db = b->GetPosition() - playerPos;
                  return da.LengthSq() < db.LengthSq();
              });
    const int count = static_cast<int>(std::min<size_t>(monsters.size(), 6));
    for (int i = 0; i < count; ++i) {
        const legend::world::MonsterCharacter* monster = monsters[i];
        const legend::math::Vector2& pos = monster->GetPosition();
        // Aggro 圈（黄，以怪物为圆心）
        DrawCircle(batch, pos, monster->GetAggroRange(),
                   legend::math::Color(1.0f, 1.0f, 0.0f, 0.55f));
        // Leash 圈（红，以 home 为圆心）
        DrawCircle(batch, monster->GetHomePosition(), monster->GetLeashRange(),
                   legend::math::Color(1.0f, 0.35f, 0.35f, 0.4f));
        // Home 十字（绿）
        const legend::math::Vector2& home = monster->GetHomePosition();
        DrawLine(batch, {home.x - 12.0f, home.y}, {home.x + 12.0f, home.y}, 3.0f,
                 legend::math::Color(0.3f, 1.0f, 0.3f, 0.9f));
        DrawLine(batch, {home.x, home.y - 12.0f}, {home.x, home.y + 12.0f}, 3.0f,
                 legend::math::Color(0.3f, 1.0f, 0.3f, 0.9f));
        // Wander Target（蓝十字）
        const auto* controller = m_worldActors.GetAIController(monster->GetId());
        if (controller != nullptr && controller->HasWanderTarget()) {
            const legend::math::Vector2& wander = controller->GetWanderTarget();
            DrawLine(batch, {wander.x - 10.0f, wander.y}, {wander.x + 10.0f, wander.y}, 3.0f,
                     legend::math::Color(0.3f, 0.6f, 1.0f, 0.9f));
            DrawLine(batch, {wander.x, wander.y - 10.0f}, {wander.x, wander.y + 10.0f}, 3.0f,
                     legend::math::Color(0.3f, 0.6f, 1.0f, 0.9f));
        }
        // 目标连线（橙）
        if (!monster->GetTargetHandle().IsEmpty()) {
            const legend::entity::Character* target =
                monster->GetTargetHandle().Resolve(m_worldActors.GetRegistry());
            if (target != nullptr) {
                DrawLine(batch, pos, target->GetPosition(), 3.0f,
                         legend::math::Color(1.0f, 0.6f, 0.1f, 0.8f));
            }
        }
    }
}

void GameScene::UpdateAITest(float deltaTime) {
    if (m_aiTestStage >= 4) {
        return;
    }
    m_aiTestElapsed += deltaTime;
    m_aiTestStageElapsed += deltaTime;
    auto monsters = m_worldActors.GetMonsters();
    auto nextStage = [this]() {
        ++m_aiTestStage;
        m_aiTestStageEntered = false;
        m_aiTestStageElapsed = 0.0;
    };
    auto fail = [this](const std::string& check, const std::string& reason) {
        ++m_aiTestFailures;
        LOG_INFO("[AITest] " + check + " FAILED: " + reason);
    };

    if (!m_player) {
        LOG_ERROR("[AITest] player missing, abort.");
        m_aiTestStage = 4;
        return;
    }

    switch (m_aiTestStage) {
    case 0: { // Aggro：传送到 Wolf 区域北缘（道路上），等待怪物进入 Chase
        if (!m_aiTestStageEntered) {
            m_aiTestStageEntered = true;
            m_player->SetPosition({4400.0f, 3170.0f});
            LOG_INFO("[AITest] stage 0 (Aggro): player teleported into wolf area (4400,3170).");
        }
        for (const legend::world::MonsterCharacter* monster : monsters) {
            if (monster->GetAIState() == legend::world::MonsterAIState::Chase &&
                monster->GetTargetHandle().GetId() == m_player->GetId()) {
                m_aiTestMonsterId = monster->GetId();
                LOG_INFO("[AggroCheck] " + monster->GetName() + "#" +
                         std::to_string(monster->GetId()) +
                         " entered Chase targeting player -> PASS");
                nextStage();
                return;
            }
        }
        if (m_aiTestStageElapsed > 10.0) {
            fail("[AggroCheck]", "no monster entered Chase within 10s.");
            nextStage();
        }
        break;
    }
    case 1: { // Leash：分段牵引玩家向东，怪物追出 leash 范围 -> ReturnHome -> 到家 Idle
        if (!m_aiTestStageEntered) {
            m_aiTestStageEntered = true;
            m_aiTestHop = 0;
            m_aiTestHopTimer = 0.0f;
            m_aiTestLeashTriggered = false;
            LOG_INFO("[AITest] stage 1 (Leash): dragging player east step by step.");
        }
        // 每 0.4s 沿道路东移 80（Wolf loseTarget=510：间隙增速 20/步，16 步后间隙 ~375 < 510，
        // 而 Wolf-home 距离可超过 leash 700 —— Slime 速度过慢无法完成本测试）
        m_aiTestHopTimer += deltaTime;
        if (m_aiTestHop < 16 && m_aiTestHopTimer >= 0.4f) {
            m_aiTestHopTimer = 0.0f;
            ++m_aiTestHop;
            const float x = 4400.0f + 80.0f * static_cast<float>(m_aiTestHop);
            m_player->SetPosition({x, 3170.0f});
            LOG_INFO("[AITest] leash drag step " + std::to_string(m_aiTestHop) + ": player -> (" +
                     std::to_string(x) + ",3170).");
        }
        const legend::world::MonsterCharacter* monster = nullptr;
        for (const legend::world::MonsterCharacter* m : monsters) {
            if (m->GetId() == m_aiTestMonsterId) {
                monster = m;
                break;
            }
        }
        if (monster == nullptr) {
            fail("[LeashCheck]", "target monster missing.");
            nextStage();
            break;
        }
        if (!m_aiTestLeashTriggered &&
            monster->GetAIState() == legend::world::MonsterAIState::ReturnHome) {
            m_aiTestLeashTriggered = true;
            LOG_INFO("[LeashCheck] " + monster->GetName() + "#" +
                     std::to_string(monster->GetId()) + " exceeded leash -> ReturnHome");
        }
        const legend::math::Vector2 toHome =
            monster->GetPosition() - monster->GetHomePosition();
        if (m_aiTestLeashTriggered && monster->GetAIState() == legend::world::MonsterAIState::Idle &&
            toHome.LengthSq() < 60.0f * 60.0f) {
            LOG_INFO("[LeashCheck] monster returned home (dist " +
                     std::to_string(std::sqrt(toHome.LengthSq())) + ") and is Idle -> PASS");
            nextStage();
            break;
        }
        if (m_aiTestStageElapsed > 40.0) {
            fail("[LeashCheck]", m_aiTestLeashTriggered
                                     ? "monster did not settle home within 40s."
                                     : "leash never triggered within 40s.");
            nextStage();
        }
        break;
    }
    case 2: { // Wander：玩家停远，任意怪物位移 > 40 视为随机游走发生
        if (!m_aiTestStageEntered) {
            m_aiTestStageEntered = true;
            m_wanderBasePositions.clear();
            for (const legend::world::MonsterCharacter* m : monsters) {
                m_wanderBasePositions.push_back(m->GetPosition());
            }
            LOG_INFO("[AITest] stage 2 (Wander): player parked at (5680,3170), watching wander.");
        }
        float maxDist = 0.0f;
        std::string mover;
        for (std::size_t i = 0; i < monsters.size() && i < m_wanderBasePositions.size(); ++i) {
            const legend::math::Vector2 delta =
                monsters[i]->GetPosition() - m_wanderBasePositions[i];
            const float dist = std::sqrt(delta.LengthSq());
            if (dist > maxDist) {
                maxDist = dist;
                mover = monsters[i]->GetName();
            }
        }
        if (maxDist > 40.0f) {
            LOG_INFO("[WanderCheck] " + mover + " moved " + std::to_string(maxDist) +
                     " units from baseline -> PASS");
            nextStage();
            break;
        }
        if (m_aiTestStageElapsed > 40.0) {
            fail("[WanderCheck]", "max displacement " + std::to_string(maxDist) +
                                      " after 40s.");
            nextStage();
        }
        break;
    }
    case 3: { // 汇总
        if (!m_aiTestSummaryDone) {
            m_aiTestSummaryDone = true;
            LOG_INFO("[AITest] completed, elapsed " + std::to_string(m_aiTestElapsed) +
                     "s, failures = " + std::to_string(m_aiTestFailures));
        }
        nextStage();
        break;
    }
    default:
        break;
    }
}

// ==================== 阶段4.1：动画推进 / 配置一致性 / 模板容错 ====================

void GameScene::RunAnimationRuntimeCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[AnimationRuntimeCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    const legend::animation::AnimationStateMachine stateMachine;

    // ---- Player：walk_east 逐帧推进 + Loop ----
    auto& playerAnim = m_player->GetAnimationPlayer();
    m_player->SetMoving(true);
    m_player->SetDirection(legend::entity::Direction8::East);
    stateMachine.Update(*m_player); // 选 walk_east（同名 Play 不重置进度）
    const int walkFrameA = playerAnim.GetCurrentFrameOrdinal();
    playerAnim.Update(0.25f); // 单帧约 0.12s -> 前进 >= 2 帧
    const int walkFrameB = playerAnim.GetCurrentFrameOrdinal();
    check("player walk_east frame advances (" + std::to_string(walkFrameA) + " -> " +
              std::to_string(walkFrameB) + ")",
          walkFrameB != walkFrameA);
    // 持续推进一个完整周期后必须 Loop 回到起点
    const int loopStart = walkFrameB;
    float advanced = 0.0f;
    bool looped = false;
    while (advanced < 5.0f) {
        playerAnim.Update(0.05f);
        advanced += 0.05f;
        if (advanced > 0.3f && playerAnim.GetCurrentFrameOrdinal() == loopStart) {
            looped = true;
            break;
        }
    }
    check("player walk_east loops back to frame " + std::to_string(loopStart), looped);

    // ---- Player：idle 两帧循环（帧时长较长，累计推进检测避免回卷歧义） ----
    m_player->SetMoving(false);
    stateMachine.Update(*m_player); // idle_east
    const int idleStart = playerAnim.GetCurrentFrameOrdinal();
    bool idleAdvanced = false;
    for (int i = 0; i < 20; ++i) {
        playerAnim.Update(0.1f); // 累计 2.0s，必跨 idle 帧
        if (playerAnim.GetCurrentFrameOrdinal() != idleStart) {
            idleAdvanced = true;
            break;
        }
    }
    check("player idle frame advances", idleAdvanced);

    // ---- NPC：Idle 逐帧循环（固定朝向，累计推进检测） ----
    const auto npcs = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::NPC);
    if (!npcs.empty()) {
        legend::entity::Character* npc = npcs.front();
        auto& npcAnim = npc->GetAnimationPlayer();
        stateMachine.Update(*npc); // idle_<固定朝向>
        const int npcStart = npcAnim.GetCurrentFrameOrdinal();
        bool npcAdvanced = false;
        for (int i = 0; i < 20; ++i) {
            npcAnim.Update(0.1f); // 累计 2.0s，必跨 idle 帧
            if (npcAnim.GetCurrentFrameOrdinal() != npcStart) {
                npcAdvanced = true;
                break;
            }
        }
        check("npc idle frame advances", npcAdvanced);
    } else {
        check("npc available for animation check", false);
    }

    // ---- Monster：walk 逐帧推进 ----
    const auto monsters =
        m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    if (!monsters.empty()) {
        legend::entity::Character* monster = monsters.front();
        auto& monsterAnim = monster->GetAnimationPlayer();
        monster->SetMoving(true);
        monster->SetDirection(legend::entity::Direction8::East);
        stateMachine.Update(*monster); // walk_east
        monsterAnim.Update(0.0f);
        const int monsterFrameA = monsterAnim.GetCurrentFrameOrdinal();
        monsterAnim.Update(0.25f);
        const int monsterFrameB = monsterAnim.GetCurrentFrameOrdinal();
        check("monster walk frame advances (" + std::to_string(monsterFrameA) + " -> " +
                  std::to_string(monsterFrameB) + ")",
              monsterFrameB != monsterFrameA);
        monster->SetMoving(false); // 还原
    } else {
        check("monster available for animation check", false);
    }

    // 还原 Player 状态（后续帧由 PlayerController/ASM 正常驱动）
    m_player->SetDirection(legend::entity::Direction8::South);
    m_player->SetMoving(false);
    LOG_INFO("[AnimationRuntimeCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunMonsterConfigCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, float actual, float expected) {
        const bool pass = std::fabs(actual - expected) < 0.001f;
        LOG_INFO("[MonsterConfigCheck] " + name + " expected " + std::to_string(expected) +
                 ", got " + std::to_string(actual) + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    struct Expected {
        const char* id;
        float aggro;
        float leash;
        float wander;
        float intervalMin;
        float intervalMax;
        float stop;
        float resume;
    };
    const Expected expected[] = {
        {"slime", 260.0f, 520.0f, 160.0f, 2.0f, 5.0f, 55.0f, 75.0f},
        {"wolf", 340.0f, 700.0f, 200.0f, 2.5f, 6.0f, 60.0f, 85.0f},
        {"boar", 300.0f, 600.0f, 180.0f, 2.0f, 5.5f, 60.0f, 80.0f},
    };
    for (const Expected& entry : expected) {
        const legend::world::MonsterDefinition* def =
            m_worldActors.GetSpawner().GetDefinition(entry.id);
        if (def == nullptr) {
            LOG_INFO(std::string("[MonsterConfigCheck] ") + entry.id +
                     " definition missing -> FAIL");
            ++failures;
            continue;
        }
        const std::string prefix = std::string(entry.id) + " ";
        check(prefix + "aggroRange", def->ai.aggroRange, entry.aggro);
        check(prefix + "leashRange", def->ai.leashRange, entry.leash);
        check(prefix + "wanderRadius", def->ai.wanderRadius, entry.wander);
        check(prefix + "wanderIntervalMin", def->ai.wanderIntervalMin, entry.intervalMin);
        check(prefix + "wanderIntervalMax", def->ai.wanderIntervalMax, entry.intervalMax);
        check(prefix + "stopDistance", def->ai.stopDistance, entry.stop);
        check(prefix + "resumeDistance", def->ai.resumeDistance, entry.resume);
    }

    // 实例化参数与模板一致：wanderInterval 已数据驱动到 MonsterCharacter
    const auto monsters =
        m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    if (!monsters.empty()) {
        const auto* monster =
            static_cast<const legend::world::MonsterCharacter*>(monsters.front());
        const legend::world::MonsterDefinition* def =
            m_worldActors.GetSpawner().GetDefinition(monster->GetMonsterTemplateId());
        if (def != nullptr) {
            check("instance wanderIntervalMin == template",
                  monster->GetWanderIntervalMin(), def->ai.wanderIntervalMin);
            check("instance wanderIntervalMax == template",
                  monster->GetWanderIntervalMax(), def->ai.wanderIntervalMax);
        }
    } else {
        LOG_INFO("[MonsterConfigCheck] monster available for config check -> FAIL");
        ++failures;
    }
    LOG_INFO("[MonsterConfigCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunMonsterTemplateFailureCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[MonsterTemplateFailureCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    // 构造指向不存在模板的临时 SpawnArea（不修改正式 monster.json）
    legend::map::MapSpawnArea badArea;
    badArea.id = 9999;
    badArea.monsterId = "nonexistent_template";
    badArea.x = 3200.0f;
    badArea.y = 3200.0f;
    badArea.count = 3;
    badArea.radius = 100.0f;

    const std::size_t before = m_worldActors.GetRegistry().Count();
    std::mt19937 testRng(12345); // 独立 rng，不影响 WorldActorManager 随机状态
    const auto spawned = m_worldActors.GetSpawner().SpawnArea(badArea, *m_map, testRng);
    check("invalid template area Spawned=0", spawned.empty());
    check("invalid template area Failed == count (3)", 3 - static_cast<int>(spawned.size()) == 3);
    check("registry unchanged after invalid spawn",
          m_worldActors.GetRegistry().Count() == before);

    // 已有合法模板与怪物不受影响
    const auto monsters =
        m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    check("existing legal monsters still 17", monsters.size() == 17);
    check("valid templates still 3", m_worldActors.GetSpawner().TemplateCount() == 3);

    LOG_INFO("[MonsterTemplateFailureCheck] completed, failures = " + std::to_string(failures));
}
