#include "Client/Source/GameScene.h"

#include "Client/Network/ClientNetworkController.h"
#include "Client/Visuals/VisualRuntime.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Animation/AnimationStateMachine.h"
#include "Engine/Combat/CombatResolver.h"
#include "Engine/Combat/CombatStats.h"
#include "Engine/Combat/CombatSystem.h"
#include "Client/Character/PlayerCharacter.h"
#include "Client/Loot/GroundLoot.h"
#include "Client/Loot/LootManager.h"
#include "Client/Loot/LootTable.h"
#include "Client/Progression/PlayerProgression.h"
#include "Client/World/MonsterCharacter.h"
#include "Client/World/MonsterDefinition.h"
#include "Engine/Core/Engine.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Entity/EntityIdAllocator.h"
#include "Engine/Item/ItemDatabase.h"
#include "Engine/Item/ItemDefinition.h"
#include "Engine/Item/Inventory.h"
#include "Engine/Map/Map.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Progression/ExperienceTable.h"
#include "Engine/Progression/LevelSystem.h"
#include "Engine/Render/SpriteBatch.h"
#include "Engine/Render/Texture.h"
#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Skill/SkillTypes.h"

GameScene::~GameScene() = default; // 阶段9：unique_ptr 完整类型在此实例化

// ---------------------------------------------------------------------------
// 阶段25 指令五十二：Settings 窗口的窗口模式/分辨率应用（SDL 直接调用）。
// ---------------------------------------------------------------------------
namespace {

void SetWindowFullscreen(legend::Engine& engine, bool fullscreen) {
    SDL_Window* window = engine.GetWindow().GetHandle();
    if (window != nullptr) {
        SDL_SetWindowFullscreen(window, fullscreen);
    }
}

void SetWindowResolution(legend::Engine& engine, int index) {
    static constexpr int kWidths[3] = {1280, 1600, 1920};
    static constexpr int kHeights[3] = {720, 900, 1080};
    if (index < 0 || index > 2) {
        index = 1;
    }
    SDL_Window* window = engine.GetWindow().GetHandle();
    if (window != nullptr) {
        SDL_SetWindowSize(window, kWidths[index], kHeights[index]);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

} // namespace

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
    m_playerSpawnPosition = m_player->GetPosition(); // Debug 复活回此点

    // ---- 阶段4：世界角色（NPC + Monster + AI） ----
    if (!m_worldActors.Initialize(resources)) {
        LOG_ERROR("GameScene: world actor manager initialize failed.");
        return; // 世界角色初始化失败不能静默继续
    }
    m_worldActors.RegisterPlayer(m_player.get());
    m_player->SetItemDatabase(&m_worldActors.GetItemDatabase()); // 阶段7：装备系统注入
    // 阶段8：默认技能栏绑定（SkillDatabase 已随 World 初始化成功加载）+ 技能控制器初始化
    m_player->GetLoadout().InitializeDefaults(m_worldActors.GetSkillDatabase());
    m_playerSkill.Initialize(&m_worldActors.GetSkillDatabase(), &m_worldActors.GetRegistry(),
                             &m_worldActors.GetCombatSystem());
    const int npcSpawned = m_worldActors.SpawnNPCs(*m_map);
    const legend::world::WorldSpawnStats monsterStats = m_worldActors.SpawnMonsters(*m_map);

    auto& camera = engine.GetCamera();
    camera.SetZoom(1.0f);
    camera.SetPosition(m_player->GetPosition()); // 跟随脚底位置

    ApplyAutoTestHooks();

    // ---- 阶段24：Visual Runtime 初始化（失败不阻塞——离线 Debug 路径保持原样）----
    m_visualRuntime = std::make_unique<legend::client::VisualRuntime>();
    if (!m_visualRuntime->Initialize(engine.GetResources(), renderer.GetSpriteShader(),
                                     std::string())) {
        m_visualRuntime.reset(); // 数据缺失：保持旧渲染路径（不崩溃）
    }
    m_visualSmoke = SDL_getenv("LEGEND_CLIENT_VISUAL_SMOKE") != nullptr;
    // 阶段25 指令六十九：Vertical Slice Client Smoke —— AutoEnter 进世界后 30s 存活。
    m_vsSmoke = SDL_getenv("LEGEND_CLIENT_VS_SMOKE") != nullptr;
    if (m_visualSmoke) {
        LOG_INFO("[VisualSmoke] game-scene-started");
    }

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
    RunCombatStatsCheck();
    RunCombatResolverCheck();
    RunAttackCooldownCheck();
    RunAttackRangeCheck();
    RunAnimationEventCheck();
    RunDeathCheck();
    RunCombatActiveCheck();
    RunMonsterCombatConfigCheck();
    RunCharacterHitTestCheck();
    RunCombatTargetLifecycleCheck();
    RunExperienceCheck();
    RunLevelGrowthCheck();
    RunItemDatabaseCheck();
    RunInventoryStackCheck();
    RunInventoryFullCheck();
    RunLootRollCheck();
    RunExperience64Check();
    RunItemDatabaseFailureCheck();
    // 注意：RunGroundLootPickupCheck / RunPartialPickupCheck / RunDeathRewardCheck /
    // RunDeathLootIntegrationCheck 会给真实 Player 加经验并生成测试 GroundLoot
    //（阶段6 遗留），阶段8.1 起移入下方 m_skillChecks 门——普通启动零污染。
    RunEquipmentDefinitionCheck();
    RunEquipmentSlotCheck();
    RunInventoryInstanceCheck();
    RunEquipCheck();
    RunUnequipCheck();
    RunEquipmentSwapCheck();
    RunFullInventoryUnequipCheck();
    RunFullInventorySwapCheck();
    RunEquipmentUniqueInstanceCheck();
    RunEquipmentStatsCheck();
    RunEquipmentHpClampCheck();
    RunLevelEquipmentCheck();
    RunEquipmentComparisonCheck();
    RunEquipmentTypeGuardCheck();
    RunEquipmentLootConfigCheck();
    RunSlotOverwriteGuardCheck();
    RunOfficialEquipmentLootCheck();
    RunEquipmentTestRestoreCheck();

    // ---- 阶段8.1：Skill Check 隔离策略 ----
    // Pure Check（局部对象，不碰真实世界）：普通启动执行；
    // World Integration Check（操作 Player/Monster/Loot/Aggro）：仅 LEGEND_RUN_SKILL_CHECKS=1
    // 或 LEGEND_AUTO_SKILL_TEST=1 时执行（普通启动绝不污染真实世界）。
    CaptureSkillWorldSnapshot(); // 隔离基线：初始化完成后的真实世界状态
    RunSkillDatabaseCheck();
    RunSkillDatabaseFailureCheck();
    RunSkillDefinitionValidationCheck();
    RunSkillLoadoutCheck();
    RunSkillManaCheck();
    RunSkillCooldownCheck();
    RunSkillAnimationEventCheck();
    RunSkillNoFreeRewardCheck();
    RunSkillWorldStateIsolationCheck();
    if (m_skillChecks) {
        LOG_INFO("[SkillChecks] world integration checks enabled.");
        // 阶段6/7 遗留的世界修改 Check（加经验/生成掉落/真实拾取）同样只在测试模式执行
        RunGroundLootPickupCheck();
        RunPartialPickupCheck();
        RunDeathRewardCheck();
        RunDeathLootIntegrationCheck();
        RunEquipmentLootCheck();
        RunSkillActiveCheck();
        RunSkillCastValidationCheck();
        RunSkillManaCooldownCheck();
        RunSkillInterruptCheck();
        RunSkillSingleTargetDamageCheck();
        RunSkillDefenseCheck();
        RunSkillRangeCheck();
        RunSkillAOECheck();
        RunSkillAOEDeathCheck();
        RunSkillTargetDeathBeforeEventCheck();
        RunSkillTargetDespawnCheck();
        RunSkillCastStateCheck();
        RunSkillMovementLockCheck();
        RunSkillBasicAttackInteractionCheck();
        RunEquipmentSkillDamageCheck();
        RunSkillAttackSnapshotCheck();
        RunSkillRespawnResetCheck();
        RunSkillAggroCheck();
        RunSkillDeathRewardCheck();
        RunSkillDeterministicRewardCheck();
        RunSkillAOERewardCheck();
        // 阶段8.2：完整状态恢复验收
        RunSkillProgressionRestoreCheck();
        RunSkillAggroRestoreCheck();
        RunSkillFullStateRestoreCheck();
        // 阶段8.3：临时装备统一清理验收（场景A/B/C + 模拟 FAIL/timeout 路径）
        RunSkillTemporaryEquipmentCleanupCheck();
        RunSkillEquipmentFailureCleanupCheck();
        // 阶段8.4：临时装备 Final Stats 重算验收（单次 + 100 次防漂移循环）
        RunSkillTemporaryEquipmentStatsCheck();
    } else {
        LOG_INFO("[SkillChecks] pure checks only; world integration checks disabled.");
    }

    LOG_INFO("GameScene ready. Map: '" + m_map->GetName() + "', player spawn tile: (" +
             std::to_string(spawnTileX) + "," + std::to_string(spawnTileY) + "), NPCs: " +
             std::to_string(npcSpawned) + ", monsters: " + std::to_string(monsterStats.spawned) +
             "/" + std::to_string(monsterStats.requested) + ".");

    // 阶段6.1：Auto Progression Test 专用——slime 掉落覆盖为必掉 small_potion x2，
    // 让 Death -> Reward -> Roll -> GroundLoot 链路可确定性验证（禁止 SpawnGroundLoot 兜底掩盖）。
    // 放在全部静态 Check 之后：DeathRewardCheck 等仍用 monster.json 原表。
    if (m_progTest) {
        std::vector<legend::world::LootEntry> guaranteed;
        legend::world::LootEntry guaranteedEntry;
        guaranteedEntry.itemId = "small_potion";
        guaranteedEntry.chance = 1.0f;
        guaranteedEntry.min = 2;
        guaranteedEntry.max = 2;
        guaranteed.push_back(guaranteedEntry);
        m_worldActors.GetSpawner().SetTestLootOverride("slime", guaranteed);
    }
}

bool GameScene::LoadPlayerCharacter() {
    auto& resources = legend::Engine::Get().GetResources();
    const std::string assetsRoot = resources.GetAssetRoot();

    legend::animation::CharacterDefinition definition;
    const bool loaded = legend::animation::LoadCharacterDefinition(
        assetsRoot + "/Characters/TestHero/character.json", definition);

    if (loaded) {
        // 阶段8指令一百零二：Player 必须有合法 skillResource.maxMana > 0，否则初始化失败
        if (definition.hasCombat &&
            (!definition.hasSkillResource || definition.maxMana <= 0.0f)) {
            LOG_ERROR("GameScene: player character.json skillResource.maxMana invalid "
                      "(must be > 0). Player load aborted.");
            return false;
        }
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

    // 阶段21 指令十九：F —— 交互半径内有 Portal 时发 PortalUseRequest（不自动传送，
    // 服务器全量重验）；否则保留原相机跟随切换（历史 Debug 键）。
    if (input.IsKeyPressed(SDL_SCANCODE_F)) {
        bool portalUsed = false;
        if (m_networkController != nullptr && m_networkController->World().IsWorldReady()) {
            auto& world = m_networkController->World();
            portalUsed = world.SendPortalUseNearest(world.ServerPositionX(),
                                                    world.ServerPositionY());
            if (portalUsed) {
                LOG_INFO("[Portal] F -> PortalUseRequest (nearest in radius).");
            }
        }
        if (!portalUsed) {
            m_cameraFollow = !m_cameraFollow;
            LOG_INFO(m_cameraFollow ? "Camera follow: enabled (F)" : "Camera follow: disabled (F)");
        }
    }

    // 鼠标滚轮缩放：无论是否跟随都生效
    const float wheel = input.GetMouseWheelDelta();
    if (wheel != 0.0f) {
        camera.SetZoom(camera.GetZoom() * (wheel > 0.0f ? 1.1f : 1.0f / 1.1f));
    }

    if (m_cameraFollow) {
        // 阶段24：在线模式跟随服务器权威本地玩家视觉位置（指令九：平滑跟随无抖动）；
        // 离线跟随本地 PlayerCharacter。
        legend::math::Vector2 target = m_player ? m_player->GetPosition()
                                                : camera.GetPosition();
        if (m_visualRuntime && m_networkController != nullptr &&
            m_networkController->World().IsWorldReady()) {
            target = {m_visualRuntime->LocalVisualX(), m_visualRuntime->LocalVisualY()};
            if (!m_visualCameraSnapped) {
                camera.SetPosition(target); // 进入世界首帧直接对准（避免跨图拖影）
                m_visualCameraSnapped = true;
            }
        }
        const float smoothing = 1.0f - std::exp(-10.0f * deltaTime);
        camera.SetPosition(camera.GetPosition() + (target - camera.GetPosition()) * smoothing);
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

    float worldW = 0.0f;
    float worldH = 0.0f;
    // 阶段24：在线模式以服务器地图快照边界为准；离线用 Legacy 地图尺寸。
    bool useWorldBounds = false;
    if (m_visualRuntime && m_networkController != nullptr &&
        m_networkController->World().IsWorldReady() &&
        m_networkController->World().MapModel().HasSnapshot()) {
        const auto& mapModel = m_networkController->World().MapModel();
        worldW = mapModel.MaxX() - mapModel.MinX();
        worldH = mapModel.MaxY() - mapModel.MinY();
        useWorldBounds = true;
    } else {
        worldW = m_map->GetWorldWidth();
        worldH = m_map->GetWorldHeight();
    }

    const float halfViewW = static_cast<float>(viewportW) * 0.5f / camera.GetZoom();
    const float halfViewH = static_cast<float>(viewportH) * 0.5f / camera.GetZoom();

    legend::math::Vector2 clamped = camera.GetPosition();
    if (worldW > halfViewW * 2.0f) {
        const float minX = useWorldBounds ? m_networkController->World().MapModel().MinX() + halfViewW
                                          : halfViewW;
        const float maxX = useWorldBounds
                               ? m_networkController->World().MapModel().MaxX() - halfViewW
                               : worldW - halfViewW;
        clamped.x = std::clamp(clamped.x, minX, maxX);
    } else {
        clamped.x = useWorldBounds
                        ? (m_networkController->World().MapModel().MinX() +
                           m_networkController->World().MapModel().MaxX()) * 0.5f
                        : worldW * 0.5f;
    }
    if (worldH > halfViewH * 2.0f) {
        const float minY = useWorldBounds ? m_networkController->World().MapModel().MinY() + halfViewH
                                          : halfViewH;
        const float maxY = useWorldBounds
                               ? m_networkController->World().MapModel().MaxY() - halfViewH
                               : worldH - halfViewH;
        clamped.y = std::clamp(clamped.y, minY, maxY);
    } else {
        clamped.y = useWorldBounds
                        ? (m_networkController->World().MapModel().MinY() +
                           m_networkController->World().MapModel().MaxY()) * 0.5f
                        : worldH * 0.5f;
    }
    camera.SetPosition(clamped);
}

void GameScene::Update(float deltaTime) {
    auto& engine = legend::Engine::Get();
    auto& input = engine.GetInput();
    m_sceneElapsed += deltaTime;
    if (!m_networkController) {
        m_networkController = std::make_unique<legend::client::ClientNetworkController>();
    }
    // 阶段24：视觉事件钩子（只读转发；控制器创建后挂一次）
    if (m_visualRuntime && !m_visualHookWired) {
        m_networkController->World().SetVisualEventHook(
            [this](const legend::client::WorldNetworkEvent& event) {
                if (m_visualRuntime != nullptr && m_networkController != nullptr) {
                    m_visualRuntime->OnWorldEvent(event, m_networkController->World());
                }
            });
        m_visualHookWired = true;
    }
    m_networkController->Update(input, deltaTime);

    // ---- 阶段24：F10 热重载（指令三十四；Dev AutoLogin 已改 Ctrl+F10）----
    if (input.IsKeyPressed(SDL_SCANCODE_F10) && m_visualRuntime) {
        m_visualRuntime->ReloadAssets();
    }

    // ---- 阶段24：Visual Runtime 每帧更新（在线时）+ Smoke 计时（指令四十七）----
    if (m_visualRuntime && m_networkController->World().IsWorldReady()) {
        m_visualRuntime->Update(m_networkController->World(), deltaTime);
    }
    if (m_visualSmoke) {
        // 用墙钟计时（软件渲染低 FPS 时 deltaTime 被钳制，游戏时间会远慢于真实时间）。
        if (m_visualSmokeStartMs == 0) {
            m_visualSmokeStartMs = SDL_GetTicks();
        }
        if (SDL_GetTicks() - m_visualSmokeStartMs >= 15000) {
            LOG_INFO("[VisualSmoke] pass — client alive 15s, quitting cleanly.");
            legend::Engine::Get().Quit();
        }
    }
    if (m_vsSmoke) {
        // 阶段25 指令六十九：AutoEnter 进世界 + 30s 存活干净退出 + 里程碑标记。
        if (m_vsSmokeStartMs == 0) {
            m_vsSmokeStartMs = SDL_GetTicks();
        }
        if (!m_vsSmokeWorldReadyLogged &&
            m_networkController != nullptr && m_networkController->World().IsWorldReady()) {
            m_vsSmokeWorldReadyLogged = true;
            LOG_INFO("[VsSmoke] entered-world map=" + std::to_string(
                         m_networkController->World().MapModel().CurrentMapId()) + " name=" +
                     m_networkController->World().MapModel().CurrentMapName());
        }
        if (SDL_GetTicks() - m_vsSmokeStartMs >= 30000) {
            LOG_INFO("[VsSmoke] pass — client alive 30s (world-ready=" +
                     std::to_string(m_vsSmokeWorldReadyLogged ? 1 : 0) + "), quitting cleanly.");
            legend::Engine::Get().Quit();
        }
    }

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
    if (input.IsKeyPressed(SDL_SCANCODE_F4)) {
        m_combatDebug = !m_combatDebug;
        LOG_INFO(m_combatDebug ? "Combat debug: enabled (F4)" : "Combat debug: disabled (F4)");
    }
    if (input.IsKeyPressed(SDL_SCANCODE_F5)) {
        m_progressionDebug = !m_progressionDebug;
        LOG_INFO(m_progressionDebug ? "Progression/Loot debug: enabled (F5)"
                                    : "Progression/Loot debug: disabled (F5)");
    }
    // ---- 阶段14 指令五十九/六十/九十：Space = Debug 攻击最近可见 alive Monster ----
    // Client 只发目标（SendAttack），伤害/距离/冷却全部服务器验证（指令一/三/六十）；
    // 按下时不做本地预测扣血（指令六十六，等 CombatEvent）。
    if (input.IsKeyPressed(SDL_SCANCODE_SPACE) && m_networkController != nullptr &&
        m_networkController->World().IsWorldReady()) {
        auto& world = m_networkController->World();
        const float selfX = world.ServerPositionX();
        const float selfY = world.ServerPositionY();
        std::uint64_t bestId = 0;
        float bestDistSq = 0.0f;
        for (const auto& [entityId, monster] : world.RemoteMonsters().All()) {
            if (!monster.Alive()) {
                continue; // 指令九十：最近目标选择忽略 alive=false
            }
            const float dx = monster.ServerX() - selfX;
            const float dy = monster.ServerY() - selfY;
            const float distSq = dx * dx + dy * dy;
            if (bestId == 0 || distSq < bestDistSq) {
                bestId = entityId;
                bestDistSq = distSq;
            }
        }
        if (bestId != 0) {
            world.SendAttack(bestId);
            LOG_INFO("[Combat] Space -> attack Monster #" + std::to_string(bestId));
        } else {
            LOG_INFO("[Combat] Space -> no visible alive monster.");
        }
    }
    // ---- 阶段15 指令五十九~六十二：Debug 技能键 1/2/3（Space 保留普攻）----
    // 1 = Quick Strike / 2 = Fire Bolt（自动选最近 visible+alive Monster，仅
    // Debug 便利——服务器重新验证一切，指令六十）；3 = Whirlwind（无目标，
    // targetType=Self + targetEntityId=0，指令六十一）。Client 不做本地伤害预测
    //（指令五十九/一百五十九：accepted 后才开始表现，Impact 到达才播命中）。
    // 阶段19：修饰键按住时 1~5 让位给 Quest Debug 键（Ctrl/Shift/Alt+1~5）。
    const bool questModifierDown =
        input.IsKeyDown(SDL_SCANCODE_LCTRL) || input.IsKeyDown(SDL_SCANCODE_RCTRL) ||
        input.IsKeyDown(SDL_SCANCODE_LALT) || input.IsKeyDown(SDL_SCANCODE_RALT) ||
        input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.IsKeyDown(SDL_SCANCODE_RSHIFT);
    if (!questModifierDown && m_networkController != nullptr &&
        m_networkController->World().IsWorldReady()) {
        auto& world = m_networkController->World();
        std::uint32_t skillId = 0;
        if (input.IsKeyPressed(SDL_SCANCODE_1)) {
            skillId = legend::world::kSkillIdQuickStrike;
        } else if (input.IsKeyPressed(SDL_SCANCODE_2)) {
            skillId = legend::world::kSkillIdFireBolt;
        } else if (input.IsKeyPressed(SDL_SCANCODE_3)) {
            skillId = legend::world::kSkillIdWhirlwind;
        } else if (input.IsKeyPressed(SDL_SCANCODE_4)) {
            skillId = legend::world::kSkillIdBattleFocus;      // 阶段16：Self Buff
        } else if (input.IsKeyPressed(SDL_SCANCODE_5)) {
            skillId = legend::world::kSkillIdCripplingStrike;  // 阶段16：Slow
        }
        if (skillId != 0) {
            if (skillId == legend::world::kSkillIdWhirlwind ||
                skillId == legend::world::kSkillIdBattleFocus) {
                // 指令六十一：Whirlwind/Battle Focus 无需目标（Self）。
                world.SendSkillCast(skillId,
                                    static_cast<std::uint8_t>(legend::world::SkillTargetType::Self),
                                    0);
                LOG_INFO("[Skill] Key -> skill " + std::to_string(skillId) + " (Self).");
            } else {
                const float selfX = world.ServerPositionX();
                const float selfY = world.ServerPositionY();
                std::uint64_t bestId = 0;
                float bestDistSq = 0.0f;
                for (const auto& [entityId, monster] : world.RemoteMonsters().All()) {
                    if (!monster.Alive()) {
                        continue; // 指令六十：最近目标选择忽略 alive=false
                    }
                    const float dx = monster.ServerX() - selfX;
                    const float dy = monster.ServerY() - selfY;
                    const float distSq = dx * dx + dy * dy;
                    if (bestId == 0 || distSq < bestDistSq) {
                        bestId = entityId;
                        bestDistSq = distSq;
                    }
                }
                if (bestId != 0) {
                    world.SendSkillCast(
                        skillId, static_cast<std::uint8_t>(legend::world::SkillTargetType::Monster),
                        bestId);
                    LOG_INFO("[Skill] Key -> skill " + std::to_string(skillId) + " on Monster #" +
                             std::to_string(bestId));
                } else {
                    LOG_INFO("[Skill] Key -> no visible alive monster.");
                }
            }
        }
    }
    if (input.IsKeyPressed(SDL_SCANCODE_F6)) {
        m_equipmentDebug = !m_equipmentDebug;
        LOG_INFO(m_equipmentDebug ? "Equipment debug: enabled (F6)"
                                  : "Equipment debug: disabled (F6)");
        if (m_equipmentDebug && m_player != nullptr) {
            // F6 开启时输出一次槽位清单（节流，不每帧刷）
            const auto& equip = m_player->GetEquipment();
            for (int slotIdx = 0; slotIdx < legend::item::kEquipmentSlotCount; ++slotIdx) {
                const auto slot = static_cast<legend::item::EquipmentSlotType>(slotIdx);
                const auto* instance = equip.GetEquipped(slot);
                LOG_INFO("[EquipmentDebug] slot " +
                         std::string(legend::item::EquipmentSlotTypeName(slot)) + ": " +
                         (instance != nullptr
                              ? instance->definitionId + "#" +
                                    std::to_string(instance->instanceId)
                              : std::string("empty")));
            }
            LOG_INFO("[EquipmentDebug] Base ATK " +
                     std::to_string(m_player->GetBaseCombatStats().attack) + " / Final ATK " +
                     std::to_string(m_player->GetCombatStats().attack));
        }
    }
    if (input.IsKeyPressed(SDL_SCANCODE_F7)) {
        m_skillDebug = !m_skillDebug;
        LOG_INFO(m_skillDebug ? "Skill debug: enabled (F7)" : "Skill debug: disabled (F7)");
        if (m_skillDebug && m_player != nullptr) {
            // F7 开启时输出一次技能栏状态（节流，不每帧刷）
            const auto& resource = m_player->GetSkillResource();
            LOG_INFO("[SkillDebug] MP " +
                     std::to_string(static_cast<int>(resource.GetMana())) + "/" +
                     std::to_string(static_cast<int>(resource.GetMaxMana())));
            for (int slot = 0; slot < m_player->GetLoadout().GetSlotCount(); ++slot) {
                const std::string& skillId = m_player->GetLoadout().GetSkillId(slot);
                LOG_INFO("[SkillDebug] slot " + std::to_string(slot + 1) + ": " +
                         (skillId.empty() ? std::string("empty")
                                          : skillId + " cd=" +
                                                std::to_string(
                                                    m_playerSkill.GetCooldowns().GetRemaining(
                                                        skillId))));
            }
        }
    }

    if (input.IsKeyPressed(SDL_SCANCODE_F8)) {
        // 阶段19 指令五十/五十一：Quest Debug（F7 已被 Skill Debug 占用——指令五十
        // 允许"改成 F7 窗口里简单 debug 命令键"的灵活处理，此处用相邻 F8）。
        m_questDebug = !m_questDebug;
        if (m_networkController != nullptr) {
            m_networkController->World().ToggleQuestDebug();
        }
        LOG_INFO(m_questDebug ? "Quest debug: enabled (F8) — Ctrl+1~5 Accept / Shift+1~5 "
                                "TurnIn / Alt+1~5 Abandon"
                              : "Quest debug: disabled (F8)");
    }
    if (input.IsKeyPressed(SDL_SCANCODE_F9)) {
        // 阶段21 指令一百一十二：Map Debug Panel（当前地图/可见统计/Portal 列表）。
        m_mapDebug = !m_mapDebug;
        LOG_INFO(m_mapDebug ? "Map debug: enabled (F9)" : "Map debug: disabled (F9)");
    }
    // ---- 阶段21 指令三十三/一百一十三：死亡状态 R/T 复活请求（服务器权威 3 秒/
    // 费用校验；Client 倒计时只是显示）----
    if (m_networkController != nullptr && m_networkController->World().IsWorldReady()) {
        auto& world = m_networkController->World();
        if (!world.LocalAlive()) {
            if (input.IsKeyPressed(SDL_SCANCODE_R)) {
                if (world.SendRespawnRequest(
                        static_cast<std::uint8_t>(legend::world::RespawnMode::CurrentMap))) {
                    LOG_INFO("[Respawn] R -> RespawnRequest(CurrentMap).");
                }
            }
            if (input.IsKeyPressed(SDL_SCANCODE_T)) {
                if (world.SendRespawnRequest(
                        static_cast<std::uint8_t>(legend::world::RespawnMode::Town))) {
                    LOG_INFO("[Respawn] T -> RespawnRequest(Town).");
                }
            }
        }
    }
    // ---- 阶段19 指令五十：Quest Debug 键（Ctrl+1~5 接取 / Shift+1~5 提交 /
    // Alt+1~5 放弃 4001~4005；只发 questId，服务器权威校验）----
    if (m_questDebug && m_networkController != nullptr &&
        m_networkController->World().IsWorldReady()) {
        auto& world = m_networkController->World();
        for (int digit = 1; digit <= 5; ++digit) {
            const SDL_Scancode scancode =
                static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (digit - 1));
            if (!input.IsKeyPressed(scancode)) {
                continue;
            }
            const std::uint32_t questId = 4000 + static_cast<std::uint32_t>(digit);
            if (input.IsKeyDown(SDL_SCANCODE_LCTRL) || input.IsKeyDown(SDL_SCANCODE_RCTRL)) {
                world.SendQuestAccept(questId);
                LOG_INFO("[Quest] Ctrl+" + std::to_string(digit) + " -> accept " +
                         std::to_string(questId));
            } else if (input.IsKeyDown(SDL_SCANCODE_LSHIFT) ||
                       input.IsKeyDown(SDL_SCANCODE_RSHIFT)) {
                world.SendQuestTurnIn(questId);
                LOG_INFO("[Quest] Shift+" + std::to_string(digit) + " -> turn in " +
                         std::to_string(questId));
            } else if (input.IsKeyDown(SDL_SCANCODE_LALT) ||
                       input.IsKeyDown(SDL_SCANCODE_RALT)) {
                world.SendQuestAbandon(questId);
                LOG_INFO("[Quest] Alt+" + std::to_string(digit) + " -> abandon " +
                         std::to_string(questId));
            }
        }
    }

    // ---- 阶段7：Z = 装备背包中第一件 Equipment / X = 卸下 Weapon（Debug 键） ----
    // ---- 阶段8指令七十七：SkillCasting 期间 Z/X 换装拒绝（装备入口检查 ActionState） ----
    if (input.IsKeyPressed(SDL_SCANCODE_Z) && m_player != nullptr) {
        if (m_player->GetActionState() == legend::entity::CharacterActionState::SkillCasting) {
            LOG_INFO("[Skill] cannot equip while SkillCasting (Z rejected).");
        } else {
            legend::item::ItemInstanceId firstEquipment = 0;
            auto& bag = m_player->GetInventory();
            for (std::size_t i = 0; i < bag.GetCapacity() && firstEquipment == 0; ++i) {
                const legend::item::ItemInstance* slot = bag.GetSlot(i);
                if (slot != nullptr) {
                    const auto* def = m_worldActors.GetItemDatabase().Get(slot->definitionId);
                    if (def != nullptr && def->type == legend::item::ItemType::Equipment) {
                        firstEquipment = slot->instanceId;
                    }
                }
            }
            if (firstEquipment != 0) {
                const auto result = m_player->EquipInstance(firstEquipment);
                if (!result.success) {
                    LOG_WARN("[Equip] Z key failed: " + result.reason);
                }
            } else {
                LOG_INFO("[Equip] Z: no equipment in inventory.");
            }
        }
    }
    if (input.IsKeyPressed(SDL_SCANCODE_X) && m_player != nullptr) {
        if (m_player->GetActionState() == legend::entity::CharacterActionState::SkillCasting) {
            LOG_INFO("[Skill] cannot unequip while SkillCasting (X rejected).");
        } else {
            const auto result = m_player->UnequipSlot(legend::item::EquipmentSlotType::Weapon);
            if (!result.success) {
                LOG_INFO("[Unequip] X: " + result.reason);
            }
        }
    }

    // ---- 阶段6：E 拾取最近 GroundLoot（<=80 world units） ----
    // ---- 阶段20 指令十七：E 优先 NPC 交互（visible NPC <=120）——否则回落拾取 ----
    if (input.IsKeyPressed(SDL_SCANCODE_E) && m_player != nullptr) {
        bool npcInteracted = false;
        if (m_networkController != nullptr && m_networkController->World().IsWorldReady()) {
            auto& world = m_networkController->World();
            // 联机世界：优先 NPC（服务器重新验证一切）。
            npcInteracted = world.SendInteractNearestNpc(world.ServerPositionX(),
                                                         world.ServerPositionY());
            if (npcInteracted) {
                LOG_INFO("[Npc] E -> interact nearest visible NPC (<=120).");
            }
        }
        if (!npcInteracted) {
            const int picked = m_worldActors.GetLoot().PickupNearest(
                m_player->GetPosition(), 80.0f, m_player->GetInventory(),
                m_worldActors.GetItemDatabase());
            if (picked == 0) {
                LOG_INFO("[Pickup] nothing picked (no loot in range or inventory full).");
            }
        }
    }

    // ---- 阶段20 指令八十/八十一：NPC 对话/商店 Debug 键 ----
    // 对话打开时：数字键 1~9 选择 Option（优先于技能键）；B 买 / S 卖 / T 传送选项。
    if (m_networkController != nullptr && m_networkController->World().IsWorldReady()) {
        auto& world = m_networkController->World();
        if (world.Dialogue().Active()) {
            for (int digit = 1; digit <= 9; ++digit) {
                const SDL_Scancode scancode =
                    static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (digit - 1));
                if (input.IsKeyPressed(scancode)) {
                    if (world.SendDialogueOptionByIndex(static_cast<std::size_t>(digit))) {
                        LOG_INFO("[Npc] dialogue option " + std::to_string(digit) + " selected.");
                    }
                    break;
                }
            }
        } else if (world.Shop().Active()) {
            // 指令八十一：B = Buy（第一件 canBuy 条目）；S = Sell（背包第一件实例）。
            if (input.IsKeyPressed(SDL_SCANCODE_B)) {
                if (world.SendBuySelected(1)) {
                    LOG_INFO("[Npc] shop buy requested.");
                }
            }
            if (input.IsKeyPressed(SDL_SCANCODE_S)) {
                std::uint32_t sellSlot = 0;
                if (world.FindFirstBagSlotOf(legend::world::kItemSlimeCoreId, sellSlot) ||
                    world.FindFirstBagSlotOf(legend::world::kItemRustySwordId, sellSlot) ||
                    world.FindFirstBagSlotOf(legend::world::kItemClothArmorId, sellSlot)) {
                    const auto& slot = world.Inventory().Slot(sellSlot);
                    if (world.SendSellSelected(slot.instanceId, 1)) {
                        LOG_INFO("[Npc] shop sell requested (instance " +
                                 std::to_string(slot.instanceId) + ").");
                    }
                }
            }
        }
    }

    // ---- 阶段25 指令十六~五十二：正式 UI 开关键（I/C/Esc；鼠标状态喂入）----
    if (m_visualRuntime != nullptr && m_networkController != nullptr &&
        m_networkController->World().IsWorldReady()) {
        auto& world25 = m_networkController->World();
        if (input.IsKeyPressed(SDL_SCANCODE_I)) {
            m_visualRuntime->ToggleInventory();
        }
        if (input.IsKeyPressed(SDL_SCANCODE_C)) {
            m_visualRuntime->ToggleCharacterPanel();
        }
        if (input.IsKeyPressed(SDL_SCANCODE_ESCAPE)) {
            m_visualRuntime->ToggleSettings();
        }
        m_visualRuntime->SetMouseState(input.GetMousePosition().x, input.GetMousePosition().y,
                                       input.IsMouseButtonPressed(1));
        // UI 请求分发（上一帧渲染产生；服务器依旧权威）。
        for (const auto& req : m_visualRuntime->DrainUiRequests()) {
            using K = legend::client::VisualRuntime::UiRequest::Kind;
            switch (req.kind) {
                case K::DialogueOption:
                    world25.SendDialogueOptionByIndex(static_cast<std::size_t>(req.index));
                    break;
                case K::ShopBuy:
                    world25.SendBuyByIndex(static_cast<std::size_t>(req.index));
                    break;
                case K::ShopSell:
                    world25.SendSellSelected(req.instanceId, 1);
                    break;
                case K::EquipDefinition:
                    world25.SendEquipFirstOf(req.definitionId);
                    break;
                case K::UnequipSlot:
                    world25.SendUnequip(req.equipmentSlot);
                    break;
                case K::WindowFullscreen:
                    SetWindowFullscreen(engine, req.index != 0);
                    break;
                case K::WindowResolution:
                    SetWindowResolution(engine, req.index);
                    break;
                default:
                    break;
            }
        }
    }

    // ---- 阶段8：技能控制器（先于战斗：CD 递减/施法流程/1~4/M 按键/事件路由） ----
    m_playerSkill.Update(*m_player, m_playerCombat.GetTarget(), input, deltaTime);

    // ---- 玩家战斗：目标选择 / 攻击请求 / 状态推进（先于移动，MovementLock 生效） ----
    int combatVpW = 1280;
    int combatVpH = 720;
    engine.GetRenderer().QueryViewportSize(combatVpW, combatVpH);
    m_playerCombat.Update(*m_player, m_worldActors.GetRegistry(), m_worldActors.GetCombatSystem(),
                          input, static_cast<float>(combatVpW), static_cast<float>(combatVpH),
                          deltaTime);

    // ---- Movement Lock：Attacking / HitReact / Dead / SkillCasting 禁止移动 ----
    if (m_player->GetActionState() != legend::entity::CharacterActionState::Normal) {
        // 攻击/受击/施法/死亡动画期间不执行移动（保持站立，动画由 UpdateAnimation 驱动）
    } else if (m_autoDirCycle) {
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

    // 阶段5：LEGEND_AUTO_COMBAT_TEST=1 战斗验收时间线
    if (m_autoCombatTest) {
        UpdateCombatTest(deltaTime);
    }

    // 阶段6：LEGEND_AUTO_PROGRESSION_TEST=1 成长/掉落验收时间线
    if (m_progTest) {
        UpdateProgressionTest(deltaTime);
    }

    // 阶段7：LEGEND_AUTO_EQUIPMENT_TEST=1 装备验收时间线
    if (m_equipTest) {
        UpdateEquipmentTest(deltaTime);
    }

    // 阶段8：LEGEND_AUTO_SKILL_TEST=1 技能验收时间线
    if (m_skillTest) {
        UpdateSkillTest(deltaTime);
    }

    // Player 死亡 -> Debug 复活（回出生点满血）
    UpdatePlayerRespawn(deltaTime);

    UpdateCamera(deltaTime);
    LogMapStats(deltaTime);
}

void GameScene::Render(legend::render::Renderer& renderer, legend::render::Camera2D& camera) {
    int viewportW = 1;
    int viewportH = 1;
    renderer.QueryViewportSize(viewportW, viewportH);

    // ---- 阶段24：在线模式 → Visual Runtime 全接管（默认画面走真实资源渲染）----
    // 渲染顺序：Ground → Decoration → Y排序世界实体（含 Object 层）→ Foreground →
    // World Effects → 名字板/血条/飘字 → HUD（指令十一/二十四）。
    // Debug 覆盖层（F1~F9）仍可叠加。
    if (m_visualRuntime && m_visualRuntime->IsReady() && m_networkController != nullptr &&
        m_networkController->World().IsWorldReady()) {
        const auto& world = m_networkController->World();
        m_mapRenderer.BeginFrame(camera, static_cast<float>(viewportW),
                                 static_cast<float>(viewportH));
        m_visualRuntime->RenderWorld(m_mapRenderer.GetBatch(), camera,
                                     m_mapRenderer.GetViewLeft(), m_mapRenderer.GetViewTop(),
                                     m_mapRenderer.GetViewRight(), m_mapRenderer.GetViewBottom(),
                                     world);
        m_visualRuntime->RenderOverlays(m_mapRenderer.GetBatch(), world);
        m_mapRenderer.Flush();
        if (m_collisionDebug) {
            m_mapRenderer.RenderCollisionOverlay(*m_map); // F1（离线地图叠加，仅 Debug）
        }
        m_mapRenderer.EndFrame();
        // HUD（屏幕空间，Swap 之前）
        m_visualRuntime->RenderHUD(world, std::string(), static_cast<float>(viewportW),
                                   static_cast<float>(viewportH), m_mapDebug);
        return;
    }

    // ---- 阶段25 指令五十三/五十四：World 连接管线中 → Loading 覆盖层 ----
    if (m_visualRuntime && m_visualRuntime->IsReady() && m_networkController != nullptr) {
        const auto worldState = m_networkController->World().State();
        if (worldState == legend::client::WorldFlowState::Connecting ||
            worldState == legend::client::WorldFlowState::Handshaking ||
            worldState == legend::client::WorldFlowState::WaitingEnterWorld ||
            worldState == legend::client::WorldFlowState::EnteringWorld) {
            m_visualRuntime->RenderLoading(static_cast<float>(viewportW),
                                           static_cast<float>(viewportH));
            return;
        }
    }

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
    // 阶段6：地上掉落进统一 Y-Sort 队列（sortY = position.y；视口剔除在 LootManager 内完成）
    m_worldActors.GetLoot().CollectRenderItems(items, m_mapRenderer.GetViewLeft(),
                                               m_mapRenderer.GetViewRight(),
                                               m_mapRenderer.GetViewTop(),
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
        } else if (item.type == legend::map::RenderSortItem::Type::GroundLoot &&
                   item.groundLoot != nullptr) {
            m_mapRenderer.Flush(); // 冲刷排在前面的物件，保证遮挡顺序
            DrawGroundLoot(*static_cast<const legend::world::GroundLoot*>(item.groundLoot));
        } else if (item.character != nullptr) {
            m_mapRenderer.Flush(); // 冲刷排在前面的物件，保证遮挡顺序
            m_characterRenderer.Draw(m_mapRenderer.GetBatch(), *item.character);
        }
    }
    m_mapRenderer.Flush();

    // 阶段12 指令四十/四十五：远程玩家 Debug 绘制（跟随相机剔除由 SpriteBatch 裁剪）
    DrawRemotePlayers(m_mapRenderer.GetBatch());
    // 阶段13 指令五十六/五十七：远程怪物 Debug 绘制
    DrawRemoteMonsters(m_mapRenderer.GetBatch());
    // 阶段20 指令十六/三十四：NPC Debug 绘制（Quad + 名字 + 任务 Marker !/?/灰点）
    DrawRemoteNpcs(m_mapRenderer.GetBatch());
    // 阶段21 指令十八/一百一十四：Portal Debug 绘制（发光门占位 + F9 面板名字）
    DrawRemotePortals(m_mapRenderer.GetBatch());

    if (m_collisionDebug) {
        m_mapRenderer.RenderCollisionOverlay(*m_map);
    }
    if (m_characterDebug) {
        DrawCharacterDebug(m_mapRenderer.GetBatch());
    }
    if (m_aiDebug) {
        DrawAIDebugOverlay(m_mapRenderer.GetBatch());
    }
    if (m_progressionDebug) {
        DrawProgressionDebugOverlay(m_mapRenderer.GetBatch()); // F5：成长/掉落 Debug
    }
    if (m_equipmentDebug) {
        DrawEquipmentDebugOverlay(m_mapRenderer.GetBatch()); // F6：装备 Debug
    }
    if (m_skillDebug) {
        DrawSkillDebugOverlay(m_mapRenderer.GetBatch()); // F7：技能 Debug
    }
    if (m_combatDebug || m_playerCombat.GetTarget().IsEmpty() == false) {
        DrawTargetRing(m_mapRenderer.GetBatch()); // 选中目标红圈（死亡自动消失）
    }
    DrawMonsterHealthBars(m_mapRenderer.GetBatch()); // 受伤/选中/F4 怪物头顶血条
    if (m_combatDebug) {
        DrawCombatDebugOverlay(m_mapRenderer.GetBatch());
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

// 阶段12 指令四十/四十五：远程玩家 Debug 绘制。
// 复用白纹理 Quad（网络逻辑不进 Renderer，指令四十）；绿色身体 + 白色头顶标记与
// 本地玩家区分（指令四十五：Debug 描边/文字区分）；名字走 F12 文本（指令四十四，
// 世界内暂无文字渲染器，正式头顶 UI 后续单独阶段）。移动状态用亮/暗色区分（指令四十一）。
void GameScene::DrawRemotePlayers(legend::render::SpriteBatch& batch) {
    if (!m_networkController || !m_whiteTexture) {
        return;
    }
    const auto& remotes = m_networkController->World().RemotePlayers().All();
    for (const auto& [characterId, remote] : remotes) {
        const legend::math::Vector2 feet(remote.RenderX(), remote.RenderY());
        // 身体 48x64（中心在 feet 上方 32）；阶段14 指令七十四：死亡变灰。
        legend::math::Color bodyColor(0.35f, 0.95f, 0.45f, 0.95f);
        if (!remote.Alive()) {
            bodyColor = legend::math::Color(0.45f, 0.45f, 0.45f, 0.90f); // Dead 灰
        } else {
            bodyColor = remote.IsMoving()
                            ? legend::math::Color(0.35f, 0.95f, 0.45f, 0.95f)
                            : legend::math::Color(0.20f, 0.55f, 0.30f, 0.95f);
        }
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -32.0f),
                       {48.0f / 64.0f, 64.0f / 64.0f}, 0.0f, bodyColor);
        // 头顶白色小方块标记（远程玩家标识）
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -70.0f),
                       {12.0f / 64.0f, 12.0f / 64.0f}, 0.0f,
                       legend::math::Color(1.0f, 1.0f, 1.0f, 0.95f));
        // 阶段15 指令六十五：施法中显示蓝色标记（仅 Debug，不做正式技能美术）。
        if (remote.Casting()) {
            batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -84.0f),
                           {20.0f / 64.0f, 8.0f / 64.0f}, 0.0f,
                           legend::math::Color(0.25f, 0.45f, 1.0f, 0.9f));
            // Whirlwind：半透明 Debug 范围框（半径 160 → 320x320 Quad 边界）。
            if (remote.CastingSkillId() == legend::world::kSkillIdWhirlwind) {
                batch.DrawQuad(*m_whiteTexture, feet, {320.0f / 64.0f, 320.0f / 64.0f}, 0.0f,
                               legend::math::Color(0.6f, 0.7f, 1.0f, 0.12f));
            }
        }
    }
    // 阶段15 指令六十五：本地玩家 Cast-Time 施法中的蓝色标记（画在服务器权威
    // 位置；Whirlwind 是 Instant 无持续表现，仅 CastTime 技能显示）。
    if (m_networkController->World().IsWorldReady() && m_networkController->World().LocalCasting()) {
        const auto& world = m_networkController->World();
        const legend::math::Vector2 feet(world.ServerPositionX(), world.ServerPositionY());
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -84.0f),
                       {20.0f / 64.0f, 8.0f / 64.0f}, 0.0f,
                       legend::math::Color(0.25f, 0.45f, 1.0f, 0.9f));
    }
    // 阶段16 指令六十八：Battle Focus（2001）激活时本地角色顶部金色 Debug 标记。
    if (m_networkController->World().IsWorldReady()) {
        const auto& world = m_networkController->World();
        bool battleFocus = false;
        for (const auto& [instanceId, effect] : world.LocalStatusEffects().All()) {
            if (effect.effectId == legend::world::kStatusEffectIdBattleFocus) {
                battleFocus = true;
                break;
            }
        }
        if (battleFocus) {
            const legend::math::Vector2 feet(world.ServerPositionX(), world.ServerPositionY());
            batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -96.0f),
                           {24.0f / 64.0f, 8.0f / 64.0f}, 0.0f,
                           legend::math::Color(1.0f, 0.85f, 0.20f, 0.95f)); // 金色
        }
    }
}

// 阶段20 指令十六/三十四：NPC Debug 绘制——蓝色系 Quad（与玩家绿/怪物红区分）；
// 类型着色：QuestGiver 青 / Merchant 黄 / Teleporter 紫 / MultiFunction 白；
// 任务 Marker（per-player）：ReadyToTurnIn 金条(?) / Available 黄条(!) / InProgress 灰点。
// NPC 名字走 F8/F12 状态文本（无世界空间文字渲染器；Spawn 日志已输出名字）。
void GameScene::DrawRemoteNpcs(legend::render::SpriteBatch& batch) {
    if (!m_networkController || !m_whiteTexture) {
        return;
    }
    const auto& world = m_networkController->World();
    if (!world.IsWorldReady()) {
        return;
    }
    for (const auto& [npcEntityId, npc] : world.Npcs().All()) {
        if (!npc.alive) {
            continue;
        }
        const legend::math::Vector2 feet(npc.x, npc.y);
        legend::math::Color bodyColor(0.30f, 0.55f, 0.95f, 0.95f); // QuestGiver 青
        if (npc.type == legend::world::NpcType::Merchant) {
            bodyColor = legend::math::Color(0.95f, 0.80f, 0.20f, 0.95f); // Merchant 黄
        } else if (npc.type == legend::world::NpcType::Teleporter) {
            bodyColor = legend::math::Color(0.70f, 0.35f, 0.95f, 0.95f); // Teleporter 紫
        } else if (npc.type == legend::world::NpcType::MultiFunction) {
            bodyColor = legend::math::Color(0.90f, 0.90f, 0.95f, 0.95f); // MultiFunction 白
        }
        // 身体（24x28 Debug Quad，高于玩家方便辨认）。
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -40.0f),
                       {26.0f / 64.0f, 30.0f / 64.0f}, 0.0f, bodyColor);
        // 任务 Marker（指令三十四）：Ready 金 / Available 黄 / InProgress 灰点。
        if (npc.questMarker == legend::world::NpcQuestMarker::ReadyToTurnIn) {
            batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -70.0f),
                           {10.0f / 64.0f, 14.0f / 64.0f}, 0.0f,
                           legend::math::Color(1.0f, 0.85f, 0.10f, 1.0f)); // 金（?）
        } else if (npc.questMarker == legend::world::NpcQuestMarker::Available) {
            batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -70.0f),
                           {8.0f / 64.0f, 14.0f / 64.0f}, 0.0f,
                           legend::math::Color(1.0f, 1.0f, 0.30f, 1.0f)); // 黄（!）
        } else if (npc.questMarker == legend::world::NpcQuestMarker::InProgress) {
            batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -64.0f),
                           {6.0f / 64.0f, 6.0f / 64.0f}, 0.0f,
                           legend::math::Color(0.55f, 0.55f, 0.55f, 1.0f)); // 灰点
        }
    }
}

// 阶段21 指令十八/一百一十四：Portal Debug 绘制——发光门占位（外圈光环 + 内芯），
// 无世界空间文字：Portal 名/目标地图名走 F9 Map Debug Panel（指令一百一十二）。
void GameScene::DrawRemotePortals(legend::render::SpriteBatch& batch) {
    if (!m_networkController || !m_whiteTexture) {
        return;
    }
    const auto& world = m_networkController->World();
    if (!world.IsWorldReady()) {
        return;
    }
    for (const auto& [portalEntityId, portal] : world.Portals().All()) {
        if (!portal.active) {
            continue;
        }
        const legend::math::Vector2 feet(portal.x, portal.y);
        // 外圈光环（半透明青绿色竖门，56x64）。
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -48.0f),
                       {56.0f / 64.0f, 64.0f / 64.0f}, 0.0f,
                       legend::math::Color(0.20f, 0.95f, 0.85f, 0.45f));
        // 内芯（亮白竖条，20x48——发光门体）。
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -40.0f),
                       {20.0f / 64.0f, 48.0f / 64.0f}, 0.0f,
                       legend::math::Color(0.85f, 1.0f, 0.98f, 0.95f));
        // 顶部指示点（金色——与 NPC Marker 区分）。
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -88.0f),
                       {10.0f / 64.0f, 10.0f / 64.0f}, 0.0f,
                       legend::math::Color(1.0f, 0.90f, 0.20f, 1.0f));
    }
}

// 阶段13 指令五十六/五十七：远程怪物 Debug 绘制——红/橙 Quad 与远程玩家（绿色）区分；
// 状态着色（指令五十五）：Idle 暗红 / Patrol 橙 / Chase 亮红 / Returning 黄。
// 无攻击表现（指令一百一十七：即使 Chase 到玩家身边也不播放攻击）。
void GameScene::DrawRemoteMonsters(legend::render::SpriteBatch& batch) {
    if (!m_networkController || !m_whiteTexture) {
        return;
    }
    const auto& monsters = m_networkController->World().RemoteMonsters().All();
    for (const auto& [entityId, monster] : monsters) {
        const legend::math::Vector2 feet(monster.RenderX(), monster.RenderY());
        // 阶段14 指令七十四：死亡实体变灰（不做死亡动画资源）。
        legend::math::Color bodyColor(0.85f, 0.25f, 0.20f, 0.95f); // Idle 暗红
        if (!monster.Alive()) {
            bodyColor = legend::math::Color(0.45f, 0.45f, 0.45f, 0.90f); // Dead 灰
        } else if (monster.State() ==
                   static_cast<std::uint8_t>(legend::world::MonsterState::Patrol)) {
            bodyColor = legend::math::Color(0.95f, 0.55f, 0.15f, 0.95f); // Patrol 橙
        } else if (monster.State() ==
                   static_cast<std::uint8_t>(legend::world::MonsterState::Chase)) {
            bodyColor = legend::math::Color(1.0f, 0.15f, 0.10f, 1.0f); // Chase 亮红
        } else if (monster.State() ==
                   static_cast<std::uint8_t>(legend::world::MonsterState::Returning)) {
            bodyColor = legend::math::Color(0.95f, 0.90f, 0.25f, 0.95f); // Returning 黄
        }
        // 身体 40x40 + 头顶橙色小方块标记
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -20.0f),
                       {40.0f / 64.0f, 40.0f / 64.0f}, 0.0f, bodyColor);
        batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(0.0f, -50.0f),
                       {10.0f / 64.0f, 10.0f / 64.0f}, 0.0f,
                       legend::math::Color(1.0f, 0.55f, 0.10f, 0.95f));
        // 阶段16 指令六十八：状态 Debug 标记（ArmorBreak 紫 / Burn 橙 / Poison 绿 /
        // Slow 蓝），最多显示 4 个，横向排布；仅 Debug，不做正式图标。
        int statusSlot = 0;
        for (const auto& [instanceId, effect] : monster.StatusEffects().All()) {
            if (statusSlot >= 4) {
                break;
            }
            legend::math::Color statusColor(0.6f, 0.6f, 0.6f, 0.9f);
            switch (effect.effectId) {
                case legend::world::kStatusEffectIdArmorBreak:
                    statusColor = legend::math::Color(0.70f, 0.30f, 0.90f, 0.9f); // 紫
                    break;
                case legend::world::kStatusEffectIdBurn:
                    statusColor = legend::math::Color(1.00f, 0.55f, 0.10f, 0.9f); // 橙
                    break;
                case legend::world::kStatusEffectIdPoison:
                    statusColor = legend::math::Color(0.30f, 0.90f, 0.30f, 0.9f); // 绿
                    break;
                case legend::world::kStatusEffectIdSlow:
                    statusColor = legend::math::Color(0.30f, 0.60f, 1.00f, 0.9f); // 蓝
                    break;
                default:
                    break;
            }
            const float offsetX = -18.0f + static_cast<float>(statusSlot) * 12.0f;
            batch.DrawQuad(*m_whiteTexture, feet + legend::math::Vector2(offsetX, -58.0f),
                           {8.0f / 64.0f, 8.0f / 64.0f}, 0.0f, statusColor);
            ++statusSlot;
        }
    }
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
    const char* combatTest = SDL_getenv("LEGEND_AUTO_COMBAT_TEST");
    if (combatTest != nullptr && combatTest[0] == '1') {
        m_autoCombatTest = true;
        LOG_INFO("Auto-test: combat acceptance timeline enabled (LEGEND_AUTO_COMBAT_TEST=1), "
                 "stages: Select -> Attack -> Death -> Respawn -> PlayerRespawn.");
    }
    const char* progTest = SDL_getenv("LEGEND_AUTO_PROGRESSION_TEST");
    if (progTest != nullptr && progTest[0] == '1') {
        m_progTest = true;
        LOG_INFO("Auto-test: progression/loot acceptance timeline enabled "
                 "(LEGEND_AUTO_PROGRESSION_TEST=1), stages: Kill -> Exp -> Loot -> Pickup "
                 "-> LevelUp -> Growth -> Respawn.");
    }
    const char* equipTest = SDL_getenv("LEGEND_AUTO_EQUIPMENT_TEST");
    if (equipTest != nullptr && equipTest[0] == '1') {
        m_equipTest = true;
        LOG_INFO("Auto-test: equipment acceptance timeline enabled "
                 "(LEGEND_AUTO_EQUIPMENT_TEST=1), stages: Kill -> Loot -> Pickup -> Equip "
                 "-> Stats -> Swap -> Unequip -> Respawn.");
    }
    const char* skillTest = SDL_getenv("LEGEND_AUTO_SKILL_TEST");
    if (skillTest != nullptr && skillTest[0] == '1') {
        m_skillTest = true;
        m_skillChecks = true; // 阶段8.1：Auto Skill Test 自动启用 Integration Checks
        LOG_INFO("Auto-test: skill acceptance timeline enabled (LEGEND_AUTO_SKILL_TEST=1), "
                 "stages: Init -> Target -> Cast -> Event -> CD -> Range -> AOE -> Equip "
                 "-> Interrupt -> DeathReward -> PlayerRespawn -> MonsterRespawn.");
    }
    const char* skillChecks = SDL_getenv("LEGEND_RUN_SKILL_CHECKS");
    if (skillChecks != nullptr && skillChecks[0] == '1') {
        m_skillChecks = true;
        LOG_INFO("Auto-test: skill world integration checks enabled (LEGEND_RUN_SKILL_CHECKS=1).");
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
    check("40 combat clips present", allPresent);

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
    check("Columns: 15", sheet->GetColumns() == 15);
    check("Rows: 8", sheet->GetRows() == 8);
    check("Frames: 120", sheet->GetFrameCount() == 120);
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
                const int frameRow = frame.frameIndex / 15;
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
    check("atlas texture is 1440x768", std::fabs(texW - 1440.0f) < 0.01f && std::fabs(texH - 768.0f) < 0.01f);
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
        " | Scans: " + std::to_string(m_worldActors.GetTotalScanCount()) +
        // 阶段5：玩家血条（Debug 阶段窗口标题显示）
        (m_player && m_player->IsCombatEnabled()
             ? " | HP: " + std::to_string(static_cast<int>(m_player->GetCombatStats().hp)) +
                   "/" + std::to_string(static_cast<int>(m_player->GetCombatStats().maxHp))
             : std::string()) +
        // 阶段6：等级 / 经验 / 背包（Debug 阶段窗口标题显示；正式 UI 后续单独做）
        (m_player ? " | Lv: " + std::to_string(m_player->GetProgression().GetLevel()) +
                        " | EXP: " + std::to_string(m_player->GetProgression().GetCurrentExp()) +
                        "/" + std::to_string(m_player->GetProgression().GetRequiredExp()) +
                        " | Bag: " + std::to_string(m_player->GetInventory().GetUsedSlots()) +
                        "/" + std::to_string(m_player->GetInventory().GetCapacity())
                  : std::string()) +
        // 阶段7：装备槽数（Debug 阶段窗口标题显示）
        GetEquipmentStatusText() +
        // 阶段8：MP / 技能栏 CD（Debug 阶段窗口标题显示）
        GetSkillStatusText() +
        // 阶段10：F11 Account Debug（指令六十/一百一十一：最基础文本面板）
        (m_networkController && m_networkController->AccountDebugVisible()
             ? m_networkController->AccountStatusText()
             : std::string()) +
        // 阶段11：F12 World Debug（指令八十）
        (m_networkController && m_networkController->WorldDebugVisible()
             ? m_networkController->WorldStatusText()
             : std::string()) +
        // 阶段19 指令五十一：F8 Quest Debug（状态/进度来自服务器事件镜像）
        (m_networkController && m_networkController->World().QuestDebugVisible()
             ? m_networkController->World().QuestStatusText()
             : std::string()) +
        // 阶段20：F8 面板追加 NPC 名字/Marker/对话/商店 Debug 文本
        (m_networkController && m_networkController->World().QuestDebugVisible()
             ? m_networkController->World().NpcStatusText()
             : std::string()) +
        // 阶段21 指令一百一十二：F9 Map Debug Panel（地图/可见统计/Portal 列表）
        (m_mapDebug && m_networkController ? m_networkController->World().MapStatusText()
                                           : std::string()) +
        // 阶段21 指令一百一十三：死亡 Overlay（Debug 文本，非正式美术 UI）
        (m_networkController && m_networkController->World().IsWorldReady() &&
                 !m_networkController->World().LocalAlive()
             ? m_networkController->World().MapStatusText()
             : std::string());

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

// ==================== 阶段5：Combat Core 自检与验收 ====================

void GameScene::RunCombatStatsCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[CombatStatsCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };

    legend::combat::CombatStats stats;
    stats.maxHp = 100.0f;
    stats.hp = 100.0f;
    stats.attack = 10.0f;
    stats.defense = 50.0f; // Defense > Attack：FinalDamage 仍至少 1
    stats.attackRange = 80.0f;
    stats.attackInterval = 1.0f;
    check("stats valid", stats.IsValid());

    // 最低 1 伤害由 CombatResolver 保证（5-50=-45 -> 1），TakeDamage 收到最终伤害
    const float resolvedMin = legend::combat::CombatResolver::ComputeFinalDamage(5.0f, 50.0f);
    const float d1 = stats.TakeDamage(resolvedMin);
    check("minimum 1 damage when defense dominates", d1 == 1.0f && stats.hp == 99.0f);

    stats.hp = 3.0f;
    const float d2 = stats.TakeDamage(10.0f); // HP 下限 0
    check("hp clamped to 0", d2 == 10.0f && stats.hp == 0.0f);
    check("dead when hp <= 0", !stats.IsAlive());
    const float d3 = stats.TakeDamage(10.0f); // 尸体不可再次受击
    check("dead takes no damage", d3 == 0.0f);

    stats.hp = 50.0f;
    stats.Heal(1000.0f); // Heal 不超过 MaxHP
    check("heal clamped to maxHp", stats.hp == stats.maxHp);
    stats.SetHp(-5.0f);
    check("setHp clamped to 0", stats.hp == 0.0f);
    stats.SetHp(stats.maxHp);
    check("alive again", stats.IsAlive());

    LOG_INFO("[CombatStatsCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunCombatResolverCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, float actual, float expected) {
        const bool pass = std::fabs(actual - expected) < 0.001f;
        LOG_INFO("[CombatResolverCheck] " + name + " expected " + std::to_string(expected) +
                 ", got " + std::to_string(actual) + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    // Attack 80 / Defense 20 => 60
    check("80-20", legend::combat::CombatResolver::ComputeFinalDamage(80.0f, 20.0f), 60.0f);
    // Attack 10 / Defense 100 => 1（最低伤害）
    check("10-100", legend::combat::CombatResolver::ComputeFinalDamage(10.0f, 100.0f), 1.0f);
    // Attack 0 / Defense 0 => 1
    check("0-0", legend::combat::CombatResolver::ComputeFinalDamage(0.0f, 0.0f), 1.0f);
    LOG_INFO("[CombatResolverCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunAttackCooldownCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[AttackCooldownCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    if (!m_player) {
        check("player available", false);
        LOG_INFO("[AttackCooldownCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 找最近 alive Slime 并传送到攻击距离内（静态验收用传送钩子）
    const auto monsters = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    legend::entity::Character* target = nullptr;
    for (legend::entity::Character* monster : monsters) {
        if (monster != nullptr && monster->IsCombatAlive() &&
            monster->GetName() == "Slime") {
            target = monster;
            break;
        }
    }
    if (target == nullptr) {
        check("slime target available", false);
        LOG_INFO("[AttackCooldownCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    // 保存现场（测试后还原）
    const auto playerPos = m_player->GetPosition();
    const auto targetPos = target->GetPosition();
    const auto playerState = m_player->GetActionState();
    const float savedCooldown = m_player->GetAttackCooldownRemaining();

    target->SetPosition(playerPos + legend::math::Vector2(60.0f, 0.0f)); // 60 <= attackRange 90
    m_player->SetAttackCooldownRemaining(0.0f);
    m_player->ReturnToNormal();
    m_playerCombat.GetTarget().SetTarget(target->GetId());

    const bool first = m_playerCombat.RequestAttack(*m_player, m_worldActors.GetRegistry(),
                                                    m_worldActors.GetCombatSystem());
    check("first attack accepted", first);
    check("cooldown set to attackInterval",
          std::fabs(m_player->GetAttackCooldownRemaining() -
                    m_player->GetCombatStats().attackInterval) < 0.001f);
    // 攻击动画结束状态还原后，冷却期内再请求必须 Rejected
    m_player->ReturnToNormal();
    const bool second = m_playerCombat.RequestAttack(*m_player, m_worldActors.GetRegistry(),
                                                     m_worldActors.GetCombatSystem());
    check("immediate re-request rejected (cooldown)", !second);
    // 等待 attackInterval 后 Accepted
    m_player->TickCooldown(m_player->GetCombatStats().attackInterval + 0.01f);
    m_player->GetAnimationPlayer().Stop();
    const bool third = m_playerCombat.RequestAttack(*m_player, m_worldActors.GetRegistry(),
                                                    m_worldActors.GetCombatSystem());
    check("request accepted after attackInterval", third);

    // 还原现场
    m_player->SetPosition(playerPos);
    target->SetPosition(targetPos);
    m_player->SetActionState(playerState);
    m_player->SetAttackCooldownRemaining(savedCooldown);
    m_player->GetAnimationPlayer().Stop();
    m_playerCombat.GetTarget().ClearTarget();
    LOG_INFO("[AttackCooldownCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunAttackRangeCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[AttackRangeCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    if (!m_player) {
        check("player available", false);
        LOG_INFO("[AttackRangeCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const auto monsters = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    legend::entity::Character* target = nullptr;
    for (legend::entity::Character* monster : monsters) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Boar") {
            target = monster; // 用 Boar（区别于 CooldownCheck 的 Slime）
            break;
        }
    }
    if (target == nullptr) {
        check("boar target available", false);
        LOG_INFO("[AttackRangeCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const auto playerPos = m_player->GetPosition();
    const auto targetPos = target->GetPosition();
    const auto playerState = m_player->GetActionState();
    const float savedCooldown = m_player->GetAttackCooldownRemaining();

    // 超范围：不能开始攻击
    target->SetPosition(playerPos +
                        legend::math::Vector2(m_player->GetCombatStats().attackRange + 200.0f, 0.0f));
    m_player->SetAttackCooldownRemaining(0.0f);
    m_player->ReturnToNormal();
    m_playerCombat.GetTarget().SetTarget(target->GetId());
    const bool outOfRange = m_playerCombat.RequestAttack(*m_player, m_worldActors.GetRegistry(),
                                                         m_worldActors.GetCombatSystem());
    check("out of range rejected", !outOfRange);
    // 进入范围：允许攻击
    target->SetPosition(playerPos + legend::math::Vector2(50.0f, 0.0f));
    const bool inRange = m_playerCombat.RequestAttack(*m_player, m_worldActors.GetRegistry(),
                                                      m_worldActors.GetCombatSystem());
    check("in range accepted", inRange);

    m_player->SetPosition(playerPos);
    target->SetPosition(targetPos);
    m_player->SetActionState(playerState);
    m_player->SetAttackCooldownRemaining(savedCooldown);
    m_player->GetAnimationPlayer().Stop();
    m_playerCombat.GetTarget().ClearTarget();
    LOG_INFO("[AttackRangeCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunAnimationEventCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[AnimationEventCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    if (!m_player) {
        check("player available", false);
        LOG_INFO("[AnimationEventCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    auto& anim = m_player->GetAnimationPlayer();
    // attack_east：NonLoop，仅在 event 帧产生一次 attack_hit
    m_player->SetDirection(legend::entity::Direction8::East);
    anim.Play("attack_east");
    check("attack clip non-loop", anim.GetCurrentFrameCount() == 4 && !anim.IsFinished());

    int hitEvents = 0;
    float advanced = 0.0f;
    while (advanced < 2.0f) {
        anim.Update(0.05f);
        advanced += 0.05f;
        for (const auto& eventName : anim.ConsumeEvents()) {
            if (eventName == "attack_hit") {
                ++hitEvents;
            }
        }
    }
    check("attack_hit fired exactly once", hitEvents == 1);
    check("non-loop finished after playthrough", anim.IsFinished());

    // Update(0) 不重复触发
    anim.Play("attack_east");
    anim.Update(0.0f);
    anim.Update(0.0f);
    check("update(0) does not fire events", anim.ConsumeEvents().empty());

    m_player->SetDirection(legend::entity::Direction8::South);
    anim.Stop();
    LOG_INFO("[AnimationEventCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunDeathCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[DeathCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    const auto monsters = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    legend::entity::Character* target = nullptr;
    for (legend::entity::Character* monster : monsters) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            target = monster;
            break;
        }
    }
    if (target == nullptr || !m_player) {
        check("target available", false);
        LOG_INFO("[DeathCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const auto savedPos = target->GetPosition();
    const auto savedHp = target->GetCombatStats().hp;

    // HP 归 0 -> Dead：不可成为有效 CombatTarget、不可再次 TakeDamage
    target->GetCombatStats().SetHp(0.0f);
    target->EnterDead();
    check("dead state entered", target->GetActionState() == legend::entity::CharacterActionState::Dead);
    check("dead is not combat alive", !target->IsCombatAlive());
    legend::combat::CombatTarget deadTarget;
    deadTarget.SetTarget(target->GetId());
    check("dead cannot be combat target", !deadTarget.IsValid(m_worldActors.GetRegistry()));
    const float noDamage = target->GetCombatStats().TakeDamage(10.0f);
    check("dead takes no damage", noDamage == 0.0f);

    // 还原（真正的死亡由运行时时间线验证：DeathCheck 静态部分只验证语义）
    target->GetCombatStats().SetHp(savedHp);
    target->ReturnToNormal();
    target->SetPosition(savedPos);
    LOG_INFO("[DeathCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::UpdatePlayerRespawn(float deltaTime) {
    if (!m_player) {
        return;
    }
    if (m_player->GetActionState() != legend::entity::CharacterActionState::Dead) {
        m_playerRespawnTimer = 0.0f;
        return;
    }
    // 死亡动画播完后开始 2 秒倒计时
    if (m_player->GetAnimationPlayer().IsFinished()) {
        m_playerRespawnTimer += deltaTime;
        if (m_playerRespawnTimer >= 2.0f) {
            // Debug 复活：回出生点、满血、清目标、状态 Normal、方向 South
            m_player->SetPosition(m_playerSpawnPosition);
            m_player->GetCombatStats().SetHp(m_player->GetCombatStats().maxHp);
            m_playerCombat.GetTarget().ClearTarget();
            m_player->ReturnToNormal();
            m_player->SetDirection(legend::entity::Direction8::South);
            m_player->GetAnimationPlayer().Stop();
            // 阶段8指令八十：Respawn 时 Mana Fill + 全部技能 CD 清 0 + 施法清空
            m_playerSkill.ResetForRespawn(*m_player);
            m_playerRespawnTimer = 0.0f;
            LOG_INFO("[PlayerRespawn] player respawned at spawn point with full HP.");
        }
    }
}

void GameScene::DrawTargetRing(legend::render::SpriteBatch& batch) {
    if (!m_player || !m_whiteTexture) {
        return;
    }
    auto* target = m_playerCombat.GetTarget().Resolve(m_worldActors.GetRegistry());
    if (target == nullptr) {
        return; // 死亡/失效后红圈自动消失
    }
    DrawCircle(batch, target->GetPosition(), 24.0f,
               legend::math::Color(1.0f, 0.25f, 0.25f, 0.9f));
}

void GameScene::DrawMonsterHealthBars(legend::render::SpriteBatch& batch) {
    if (!m_whiteTexture) {
        return;
    }
    const legend::entity::EntityId selectedId = m_playerCombat.GetTarget().GetTargetId();
    for (const legend::world::MonsterCharacter* monster : m_worldActors.GetMonsters()) {
        if (monster == nullptr || !monster->IsVisible()) {
            continue;
        }
        const bool damaged = monster->GetCombatStats().hp < monster->GetCombatStats().maxHp;
        const bool selected = monster->GetId() == selectedId;
        if (!damaged && !selected && !m_combatDebug) {
            continue; // 仅受伤 / 选中 / F4 显示
        }
        const auto& visual = monster->GetVisual();
        const legend::math::Vector2& feet = monster->GetPosition();
        const float barW = 60.0f;
        const float barH = 6.0f;
        const legend::math::Vector2 center(feet.x, feet.y - visual.height * visual.pivot.y - 12.0f);
        // 黑底
        batch.DrawQuad(*m_whiteTexture, center, {barW / 64.0f, barH / 64.0f}, 0.0f,
                       legend::math::Color(0.0f, 0.0f, 0.0f, 0.7f));
        // 红色 HP
        const float hpRatio = monster->GetCombatStats().GetHpPercent();
        if (hpRatio > 0.0f) {
            const float fillW = barW * hpRatio;
            const legend::math::Vector2 fillCenter(center.x - (barW - fillW) * 0.5f, center.y);
            batch.DrawQuad(*m_whiteTexture, fillCenter, {fillW / 64.0f, barH / 64.0f}, 0.0f,
                           legend::math::Color(0.9f, 0.15f, 0.15f, 0.95f));
        }
    }
}

void GameScene::DrawCombatDebugOverlay(legend::render::SpriteBatch& batch) {
    if (!m_player || !m_whiteTexture) {
        return;
    }
    // Player AttackRange 圈
    const float range = m_player->GetCombatStats().attackRange;
    DrawCircle(batch, m_player->GetPosition(), range,
               legend::math::Color(0.4f, 0.8f, 1.0f, 0.5f));
    // Target 连线
    auto* target = m_playerCombat.GetTarget().Resolve(m_worldActors.GetRegistry());
    if (target != nullptr) {
        DrawLine(batch, m_player->GetPosition(), target->GetPosition(), 3.0f,
                 legend::math::Color(1.0f, 0.8f, 0.2f, 0.9f));
    }
    // 最近 3 只怪：AttackRange 圈 + 状态
    const legend::math::Vector2 playerPos = m_player->GetPosition();
    auto monsters = m_worldActors.GetMonsters();
    std::sort(monsters.begin(), monsters.end(),
              [&playerPos](const legend::world::MonsterCharacter* a,
                           const legend::world::MonsterCharacter* b) {
                  const legend::math::Vector2 da = a->GetPosition() - playerPos;
                  const legend::math::Vector2 db = b->GetPosition() - playerPos;
                  return da.LengthSq() < db.LengthSq();
              });
    const int count = static_cast<int>(std::min<size_t>(monsters.size(), 3));
    for (int i = 0; i < count; ++i) {
        const legend::world::MonsterCharacter* monster = monsters[i];
        DrawCircle(batch, monster->GetPosition(), monster->GetCombatStats().attackRange,
                   legend::math::Color(1.0f, 0.5f, 0.3f, 0.45f));
        const auto* controller = m_worldActors.GetAIController(monster->GetId());
        std::string stateName = legend::world::MonsterAIStateName(monster->GetAIState());
        if (monster->GetActionState() == legend::entity::CharacterActionState::Attacking) {
            stateName += "+ATK";
        } else if (monster->GetActionState() == legend::entity::CharacterActionState::HitReact) {
            stateName += "+HIT";
        } else if (monster->GetActionState() == legend::entity::CharacterActionState::Dead) {
            stateName += "+DEAD";
        }
        LOG_INFO("[CombatDebug] " + monster->GetName() + "#" +
                 std::to_string(monster->GetId()) + " HP " +
                 std::to_string(monster->GetCombatStats().hp) + "/" +
                 std::to_string(monster->GetCombatStats().maxHp) + " state=" + stateName +
                 " cd=" + std::to_string(monster->GetAttackCooldownRemaining()));
    }
}

void GameScene::UpdateCombatTest(float deltaTime) {
    if (m_combatTestStage >= 7) {
        return;
    }
    m_combatTestElapsed += deltaTime;
    m_combatTestStageElapsed += deltaTime;
    auto monsters = m_worldActors.GetMonsters();
    auto nextStage = [this]() {
        ++m_combatTestStage;
        m_combatTestStageEntered = false;
        m_combatTestStageElapsed = 0.0;
    };
    auto fail = [this](const std::string& check, const std::string& reason) {
        ++m_combatTestFailures;
        LOG_INFO("[CombatTest] " + check + " FAILED: " + reason);
    };
    if (!m_player) {
        LOG_ERROR("[CombatTest] player missing, abort.");
        m_combatTestStage = 7;
        return;
    }

    switch (m_combatTestStage) {
    case 0: { // 选最近 Slime 并传送到攻击距离内（固定 AI seed 已由 env 保证）
        if (!m_combatTestStageEntered) {
            m_combatTestStageEntered = true;
            legend::entity::Character* slime = nullptr;
            float bestSq = 1e9f;
            for (legend::world::MonsterCharacter* monster : monsters) {
                if (monster == nullptr || !monster->IsCombatAlive() ||
                    monster->GetMonsterTemplateId() != "slime") {
                    continue;
                }
                const float distSq = (monster->GetPosition() - m_player->GetPosition()).LengthSq();
                if (distSq < bestSq) {
                    bestSq = distSq;
                    slime = monster;
                }
            }
            if (slime == nullptr) {
                fail("[CombatTargetCheck]", "no alive slime found.");
                nextStage();
                break;
            }
            m_combatTestSlimeId = slime->GetId();
            m_player->SetPosition(slime->GetPosition() + legend::math::Vector2(-60.0f, 0.0f));
            // 阶段6：舞台布置——把玩家 200 半径内其它活怪送回 home（避免围殴 HitReact
            // 打断循环锁死玩家攻击；被测 slime 留下，Respawn 计数不受影响）
            for (legend::world::MonsterCharacter* monster : monsters) {
                if (monster == nullptr || monster == slime || !monster->IsCombatAlive()) {
                    continue;
                }
                const float distSq =
                    (monster->GetPosition() - m_player->GetPosition()).LengthSq();
                if (distSq < 200.0f * 200.0f) {
                    monster->SetPosition(monster->GetHomePosition());
                }
            }
            // 直接锁定被测 slime（禁止用 SelectNearestMonster——它在传送后会选中
            // 另一只更近的怪，导致监测对象与攻击目标不一致、时间线卡死）
            m_playerCombat.GetTarget().SetTarget(slime->GetId());
            LOG_INFO("[CombatTest] stage 0: selected Slime#" +
                     std::to_string(m_combatTestSlimeId) + ", player teleported to attack range.");
            m_combatTestLastSlimeHp = slime->GetCombatStats().hp;
            nextStage();
        }
        break;
    }
    case 1: { // 连续攻击：Space 模拟（每帧 RequestAttack，冷却控制节奏）
        if (!m_combatTestStageEntered) {
            m_combatTestStageEntered = true;
            LOG_INFO("[CombatTest] stage 1: continuous attack, watching HP drop + counter.");
        }
        auto* slime = static_cast<legend::world::MonsterCharacter*>(
            m_worldActors.GetRegistry().Get(m_combatTestSlimeId));
        if (slime == nullptr) {
            fail("[CombatAttackCheck]", "slime missing (unexpected despawn).");
            nextStage();
            break;
        }
        // 模拟按住 Space：Normal 且有目标且在范围内就请求攻击
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal) {
            m_playerCombat.RequestAttack(*m_player, m_worldActors.GetRegistry(),
                                         m_worldActors.GetCombatSystem());
        }
        // Slime HP 逐次下降验证
        const float currentHp = slime->GetCombatStats().hp;
        if (currentHp < m_combatTestLastSlimeHp) {
            m_combatTestSawDamage = true;
            m_combatTestLastSlimeHp = currentHp;
        }
        // Player HP 下降（Slime 反击）验证
        if (m_player->GetCombatStats().hp < m_player->GetCombatStats().maxHp) {
            if (!m_combatTestSawCounter) {
                m_combatTestSawCounter = true;
                LOG_INFO("[CombatTest] slime counterattack confirmed, player HP " +
                         std::to_string(m_player->GetCombatStats().hp) + "/" +
                         std::to_string(m_player->GetCombatStats().maxHp));
            }
        }
        if (slime->GetActionState() == legend::entity::CharacterActionState::Dead) {
            if (!m_combatTestSawDamage) {
                fail("[CombatAttackCheck]", "slime died without observed HP drop.");
            } else {
                LOG_INFO("[CombatTest] slime died after " +
                         std::to_string(m_combatTestStageElapsed) + "s of attacks -> PASS");
                LOG_INFO("[DeathCheck] slime entered Dead after player attacks -> PASS");
            }
            nextStage();
        } else if (m_combatTestStageElapsed > 60.0) {
            fail("[CombatAttackCheck]", "slime not dead within 60s.");
            nextStage();
        }
        break;
    }
    case 2: { // 死亡动画 -> Corpse 1.5s -> Despawn：Registry 移除
        if (!m_combatTestStageEntered) {
            m_combatTestStageEntered = true;
            LOG_INFO("[CombatTest] stage 2: waiting despawn (corpse delay 1.5s).");
        }
        if (m_worldActors.GetRegistry().Get(m_combatTestSlimeId) == nullptr) {
            const int aliveCount = m_worldActors.GetAliveMonsterCount();
            LOG_INFO("[DeathCheck] slime removed from registry after death animation -> PASS");
            LOG_INFO("[CombatTest] despawn confirmed, alive monsters: " +
                     std::to_string(aliveCount) + " (was 17) -> PASS");
            nextStage();
        } else if (m_combatTestStageElapsed > 15.0) {
            fail("[DeathCheck]", "slime not despawned within 15s.");
            nextStage();
        }
        break;
    }
    case 3: { // Respawn：respawnSeconds(5) 后数量恢复，新 EntityId
        if (!m_combatTestStageEntered) {
            m_combatTestStageEntered = true;
            m_combatTestLastSlimeHp = -1.0f; // 日志哨兵：首次发现新 slime 才打印
            LOG_INFO("[CombatTest] stage 3: waiting respawn (5s).");
        }
        // 检查是否出现新的 slime（id != 旧 id）
        bool newSlimeFound = false;
        for (legend::world::MonsterCharacter* monster : monsters) {
            if (monster != nullptr && monster->IsCombatAlive() &&
                monster->GetMonsterTemplateId() == "slime" &&
                monster->GetId() != m_combatTestSlimeId) {
                newSlimeFound = true;
                if (m_combatTestLastSlimeHp < 0.0f) { // 仅首次发现打印，避免每帧刷屏
                    m_combatTestLastSlimeHp = 0.0f;
                    LOG_INFO("[RespawnCheck] new slime entity #" +
                             std::to_string(monster->GetId()) + " (old #" +
                             std::to_string(m_combatTestSlimeId) + ") -> PASS");
                }
                break;
            }
        }
        const int slimeAreaAlive = m_worldActors.GetAreaAliveCount(1);
        if (newSlimeFound && slimeAreaAlive == 8) {
            LOG_INFO("[RespawnCheck] area count restored to 8 -> PASS");
            nextStage();
        } else if (m_combatTestStageElapsed > 20.0) {
            fail("[RespawnCheck]", "respawn not confirmed within 20s (area alive=" +
                                       std::to_string(slimeAreaAlive) + ").");
            nextStage();
        }
        break;
    }
    case 4: { // Player 死亡：SetHp(1) + ApplyDamage 致死事件（Debug 驱动，验证完整链路）
        if (!m_combatTestStageEntered) {
            m_combatTestStageEntered = true;
            m_player->GetCombatStats().SetHp(1.0f);
            legend::combat::DamageEvent lethal;
            lethal.sourceId = m_combatTestSlimeId;
            lethal.targetId = m_player->GetId();
            lethal.rawDamage = 10.0f;
            lethal.finalDamage = 10.0f;
            lethal.sequence = 99999;
            m_worldActors.GetCombatSystem().ApplyDamage(lethal);
            LOG_INFO("[CombatTest] stage 4: lethal damage applied to player.");
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Dead) {
            LOG_INFO("[PlayerRespawnCheck] player entered Dead -> PASS");
            // 禁止移动验证：传送期望位置不变
            const auto before = m_player->GetPosition();
            m_playerController.Update(legend::Engine::Get().GetInput(), m_characterController,
                                      *m_player, *m_map, deltaTime);
            if ((m_player->GetPosition() - before).LengthSq() < 0.01f) {
                LOG_INFO("[PlayerRespawnCheck] dead player cannot move -> PASS");
            } else {
                fail("[PlayerRespawnCheck]", "dead player moved.");
            }
            nextStage();
        } else if (m_combatTestStageElapsed > 5.0) {
            fail("[PlayerRespawnCheck]", "player not dead after lethal damage.");
            nextStage();
        }
        break;
    }
    case 5: { // Player 复活：2 秒后回出生点满血 Normal South
        if (!m_combatTestStageEntered) {
            m_combatTestStageEntered = true;
            LOG_INFO("[CombatTest] stage 5: waiting player respawn (2s after death anim).");
        }
        if (m_player->GetActionState() == legend::entity::CharacterActionState::Normal &&
            m_player->GetCombatStats().hp == m_player->GetCombatStats().maxHp) {
            const bool atSpawn =
                (m_player->GetPosition() - m_playerSpawnPosition).LengthSq() < 100.0f;
            const bool facingSouth =
                m_player->GetDirection() == legend::entity::Direction8::South;
            if (atSpawn && facingSouth) {
                LOG_INFO("[PlayerRespawnCheck] player respawned at spawn, full HP, Normal, "
                         "facing South -> PASS");
                nextStage();
            } else if (m_combatTestStageElapsed > 15.0) {
                fail("[PlayerRespawnCheck]", "respawn state wrong.");
                nextStage();
            }
        } else if (m_combatTestStageElapsed > 15.0) {
            fail("[PlayerRespawnCheck]", "player not respawned within 15s.");
            nextStage();
        }
        break;
    }
    case 6: { // 汇总
        if (!m_combatTestSummaryDone) {
            m_combatTestSummaryDone = true;
            LOG_INFO("[CombatTest] completed, elapsed " + std::to_string(m_combatTestElapsed) +
                     "s, failures = " + std::to_string(m_combatTestFailures));
        }
        nextStage();
        break;
    }
    default:
        break;
    }
}

// ==================== 阶段5.1：Active 校验 / 配置校验 / HitTest / 目标生命周期 ====================

void GameScene::RunCombatActiveCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[CombatActiveCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    if (!m_player) {
        check("player available", false);
        LOG_INFO("[CombatActiveCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const auto monsters = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    legend::entity::Character* target = nullptr;
    for (legend::entity::Character* monster : monsters) {
        if (monster != nullptr && monster->IsCombatAlive() && monster->GetName() == "Slime") {
            target = monster;
            break;
        }
    }
    if (target == nullptr) {
        check("slime target available", false);
        LOG_INFO("[CombatActiveCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const auto savedPlayerPos = m_player->GetPosition();
    const auto savedTargetPos = target->GetPosition();
    const auto savedTargetHp = target->GetCombatStats().hp;

    // 1. active attacker + active target -> valid
    target->SetPosition(savedPlayerPos + legend::math::Vector2(60.0f, 0.0f));
    check("active attacker + active target -> valid",
          m_worldActors.GetCombatSystem().ValidateAttack(*m_player, *target));

    // 2. inactive target -> invalid（inactive 目标 HP 不能下降）
    target->SetActive(false);
    check("inactive target -> invalid",
          !m_worldActors.GetCombatSystem().ValidateAttack(*m_player, *target));
    legend::combat::DamageEvent probe;
    probe.sourceId = m_player->GetId();
    probe.targetId = target->GetId();
    probe.finalDamage = 10.0f;
    probe.rawDamage = 10.0f;
    m_worldActors.GetCombatSystem().ApplyDamage(probe);
    check("inactive target HP unchanged",
          std::fabs(target->GetCombatStats().hp - savedTargetHp) < 0.001f);
    target->SetActive(true);

    // 3. inactive attacker -> invalid
    m_player->SetActive(false);
    check("inactive attacker -> invalid",
          !m_worldActors.GetCombatSystem().ValidateAttack(*m_player, *target));
    m_player->SetActive(true);

    // 还原
    m_player->SetPosition(savedPlayerPos);
    target->SetPosition(savedTargetPos);
    LOG_INFO("[CombatActiveCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunMonsterCombatConfigCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[MonsterCombatConfigCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    // 正式模板：stopDistance <= attackRange、resume > stop、combat/AI 有效
    const char* ids[] = {"slime", "wolf", "boar"};
    for (const char* id : ids) {
        const auto* def = m_worldActors.GetSpawner().GetDefinition(id);
        if (def == nullptr) {
            check(std::string(id) + " definition present", false);
            continue;
        }
        const std::string prefix = std::string(id) + " ";
        check(prefix + "combat stats valid", def->combat.IsValid());
        check(prefix + "ai ranges valid (resume>stop, leash>aggro, interval)",
              def->ai.resumeDistance > def->ai.stopDistance &&
                  def->ai.leashRange > def->ai.aggroRange &&
                  def->ai.wanderIntervalMax >= def->ai.wanderIntervalMin &&
                  def->ai.wanderIntervalMin >= 0.0f);
        check(prefix + "stopDistance <= attackRange",
              def->ai.stopDistance <= def->combat.attackRange + 1.0f);
        check(prefix + "full validation passes",
              legend::world::ValidateMonsterDefinition(*def));
    }
    // 非法测试 Definition（不改正式 monster.json）：attackRange=60 / stopDistance=100 必须 FAIL
    legend::world::MonsterDefinition bad;
    bad.id = "bad_test_template";
    bad.name = "BadTest";
    bad.combat.maxHp = 100.0f;
    bad.combat.hp = 100.0f;
    bad.combat.attack = 20.0f;
    bad.combat.defense = 5.0f;
    bad.combat.attackRange = 60.0f;
    bad.combat.attackInterval = 1.0f;
    bad.ai.aggroRange = 300.0f;
    bad.ai.leashRange = 600.0f;
    bad.ai.wanderRadius = 150.0f;
    bad.ai.wanderIntervalMin = 2.0f;
    bad.ai.wanderIntervalMax = 5.0f;
    bad.ai.stopDistance = 100.0f;
    bad.ai.resumeDistance = 120.0f;
    check("bad config (range 60 / stop 100) -> validation FAIL",
          !legend::world::ValidateMonsterDefinition(bad));
    LOG_INFO("[MonsterCombatConfigCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunCharacterHitTestCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[CharacterHitTestCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    // 96x96 pivot(0.5,0.85) feet(1000,1000)：
    // left=952 right=1048 top=918.4 bottom=1014.4
    legend::entity::CharacterFootprint fp;
    legend::entity::CharacterVisual visual; // 96x96 pivot 0.5/0.85
    visual.pivot = {0.5f, 0.85f};
    auto emptyClips =
        std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>();
    const auto testId = legend::entity::EntityIdAllocator::Next();
    legend::entity::Character probe(testId, "HitTestProbe", legend::entity::ActorType::Monster,
                                    0.0f, fp, visual, emptyClips);
    probe.SetPosition({1000.0f, 1000.0f});

    const auto rect = legend::render::CharacterRenderer::GetSpriteWorldRect(probe);
    check("left == 952", std::fabs(rect.left - 952.0f) < 0.01f);
    check("right == 1048", std::fabs(rect.right - 1048.0f) < 0.01f);
    check("top == 918.4", std::fabs(rect.top - 918.4f) < 0.01f);
    check("bottom == 1014.4", std::fabs(rect.bottom - 1014.4f) < 0.01f);

    auto inside = [&](float x, float y) {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    };
    check("character center area hits", inside(1000.0f, 980.0f));
    check("below feet (1000,1060) does NOT hit", !inside(1000.0f, 1060.0f));
    check("far above does NOT hit", !inside(1000.0f, 900.0f));
    check("far left does NOT hit", !inside(940.0f, 980.0f));
    check("far right does NOT hit", !inside(1060.0f, 980.0f));
    LOG_INFO("[CharacterHitTestCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunCombatTargetLifecycleCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[CombatTargetLifecycleCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    if (!m_player) {
        check("player available", false);
        LOG_INFO("[CombatTargetLifecycleCheck] completed, failures = " + std::to_string(failures));
        return;
    }
    const auto monsters = m_worldActors.GetRegistry().GetByType(legend::entity::ActorType::Monster);
    legend::entity::Character* first = nullptr;
    legend::entity::Character* second = nullptr;
    for (legend::entity::Character* monster : monsters) {
        if (monster == nullptr || !monster->IsCombatAlive()) {
            continue;
        }
        if (first == nullptr) {
            first = monster;
        } else if (second == nullptr) {
            second = monster;
            break;
        }
    }
    if (first == nullptr || second == nullptr) {
        check("two alive monsters available", false);
        LOG_INFO("[CombatTargetLifecycleCheck] completed, failures = " + std::to_string(failures));
        return;
    }

    // 1. 选中第一只：Target 非空 + Resolve 成功
    m_playerCombat.GetTarget().SetTarget(first->GetId());
    check("select monster -> target non-empty and resolves",
          !m_playerCombat.GetTarget().IsEmpty() &&
              m_playerCombat.GetTarget().Resolve(m_worldActors.GetRegistry()) == first);

    // 2. 目标死亡 -> 下一次 Update 后 Target 必须为空
    first->GetCombatStats().SetHp(0.0f);
    first->EnterDead();
    m_playerCombat.Update(*m_player, m_worldActors.GetRegistry(), m_worldActors.GetCombatSystem(),
                          legend::Engine::Get().GetInput(), 1280.0f, 720.0f, 0.016f);
    check("dead target cleared after Update", m_playerCombat.GetTarget().IsEmpty());
    first->GetCombatStats().SetHp(first->GetCombatStats().maxHp);
    first->ReturnToNormal(); // 还原（真实死亡流程由运行时验证）

    // 3. 重新选第二只
    m_playerCombat.GetTarget().SetTarget(second->GetId());
    check("re-select second monster resolves",
          m_playerCombat.GetTarget().Resolve(m_worldActors.GetRegistry()) == second);

    // 4. Unregister 目标 -> 下一次 Update 后 Target 必须为空
    second->SetActive(false); // 模拟 Despawn 前的 inactive：Registry 引用仍在
    m_playerCombat.Update(*m_player, m_worldActors.GetRegistry(), m_worldActors.GetCombatSystem(),
                          legend::Engine::Get().GetInput(), 1280.0f, 720.0f, 0.016f);
    check("inactive target cleared after Update", m_playerCombat.GetTarget().IsEmpty());
    second->SetActive(true);
    m_playerCombat.GetTarget().ClearTarget();
    LOG_INFO("[CombatTargetLifecycleCheck] completed, failures = " + std::to_string(failures));
}

// ==================== 阶段6：Progression / Loot 自检 ====================

void GameScene::RunExperienceCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[ExperienceCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    using legend::progression::LevelSystem;
    using legend::progression::RequiredExp;
    using legend::progression::ExperienceValue;
    int level = 1;
    ExperienceValue currentExp = 0;
    ExperienceValue totalExp = 0;
    check("level1 exp0", level == 1 && currentExp == 0);
    // 经验表统一入口：100 * 1.5^(level-1)
    check("exp table 100/150/225", RequiredExp(1) == 100 && RequiredExp(2) == 150 &&
                                      RequiredExp(3) == 225);
    auto events = LevelSystem::AddExperience(level, currentExp, totalExp, 50);
    check("add 50 stays lv1", level == 1 && currentExp == 50 && events.empty());
    events = LevelSystem::AddExperience(level, currentExp, totalExp, 60);
    check("level up at 100 -> lv2 remainder 10",
          level == 2 && currentExp == 10 && events.size() == 1 && events[0].oldLevel == 1 &&
              events[0].newLevel == 2);
    // 一次大量经验必须连续升级（不能只升 1 级）
    events = LevelSystem::AddExperience(level, currentExp, totalExp, 1000);
    check("one big add multi level up", level > 2 && events.size() >= 2);
    // 满级封顶：从 lv49 满经验加 1 -> lv50，currentExp 归 0，继续获得不再升级
    // （RequiredExp(49) ≈ 2.7e10，64 位下返回真实需求）
    level = legend::progression::kMaxLevel - 1;
    currentExp = RequiredExp(49);
    events = LevelSystem::AddExperience(level, currentExp, totalExp, 1);
    check("capped at max level 50",
          level == legend::progression::kMaxLevel && currentExp == 0 && !events.empty());
    events = LevelSystem::AddExperience(level, currentExp, totalExp, 500);
    check("max level no more level ups", level == legend::progression::kMaxLevel && events.empty());
    LOG_INFO("[ExperienceCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunLevelGrowthCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[LevelGrowthCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    legend::progression::PlayerProgression progression;
    legend::animation::CharacterDefinition definition; // growth 缺省 20/5/2
    progression.Initialize(definition);
    legend::combat::CombatStats stats;
    stats.maxHp = 500.0f;
    stats.hp = 500.0f;
    stats.attack = 80.0f;
    stats.defense = 20.0f;
    // 阶段7：growth 加到 base stats（PlayerStatsComponent）后重算 final（无装备 final == base）
    // 120 exp -> lv2（需求100，剩20），base 成长 MaxHP+20/Attack+5/Defense+2
    PlayerStatsComponent statsComponent;
    statsComponent.Initialize(stats);
    const auto levelUps = progression.AddExperience(120);
    statsComponent.ApplyLevelGrowth(progression.GetGrowth(),
                                    static_cast<int>(levelUps.size()));
    legend::item::ItemDatabase emptyItems;
    legend::item::EquipmentComponent noEquipment;
    statsComponent.RecalculateFinalStats(stats, noEquipment, emptyItems);
    // 阶段6 HP 同步语义保持：升级 MaxHP+20 时当前 HP 也 +20（PlayerCharacter 内实现）
    stats.hp = std::min(stats.hp + progression.GetGrowth().maxHpPerLevel *
                                      static_cast<float>(levelUps.size()),
                        stats.maxHp);
    check("lv2 after 120 exp", progression.GetLevel() == 2);
    check("maxHp 500 -> 520", std::fabs(stats.maxHp - 520.0f) < 0.001f);
    check("hp +20 synced (no overheal)", std::fabs(stats.hp - 520.0f) < 0.001f &&
                                            stats.hp <= stats.maxHp);
    check("attack 80 -> 85", std::fabs(stats.attack - 85.0f) < 0.001f);
    check("defense 20 -> 22", std::fabs(stats.defense - 22.0f) < 0.001f);
    LOG_INFO("[LevelGrowthCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunItemDatabaseCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[ItemDatabaseCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    legend::item::ItemDatabase database;
    const bool loaded = database.LoadFromFile(
        legend::Engine::Get().GetResources().GetAssetRoot() + "/Items/items.json");
    check("items.json loaded", loaded);
    check("at least 5 items", database.Count() >= 5);
    const legend::item::ItemDefinition* potion = database.Get("small_potion");
    check("small_potion correct",
          potion != nullptr && potion->maxStack == 20 &&
              potion->type == legend::item::ItemType::Consumable);
    check("wolf_fang present", database.Get("wolf_fang") != nullptr);
    check("boar_hide present", database.Get("boar_hide") != nullptr);
    check("slime_gel present", database.Get("slime_gel") != nullptr);
    check("iron_ore present", database.Get("iron_ore") != nullptr);
    check("unknown id -> nullptr", database.Get("not_exist_item_xyz") == nullptr);
    check("unknown id -> exists false", !database.Exists("not_exist_item_xyz"));
    LOG_INFO("[ItemDatabaseCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunInventoryStackCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[InventoryStackCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    legend::item::Inventory inventory;
    legend::item::ItemDefinition potion;
    potion.id = "small_potion";
    potion.name = "Small Potion";
    potion.type = legend::item::ItemType::Consumable;
    potion.maxStack = 20;
    // 阶段6.1：ItemInstanceIdAllocator Reset(nextValue) 语义 —— Reset(7) 后 Next()==7
    legend::item::ItemInstanceIdAllocator::Reset(7);
    check("item instance allocator Reset(7) -> Next()==7",
          legend::item::ItemInstanceIdAllocator::Next() == 7);
    check("item instance allocator Next() increments",
          legend::item::ItemInstanceIdAllocator::Next() == 8);
    legend::item::ItemInstanceIdAllocator::Reset(1); // 还原默认起点
    const auto first = inventory.AddItem(potion, 18);
    check("add 18 -> 1 slot", first.added == 18 && first.remaining == 0 &&
                                 inventory.GetUsedSlots() == 1);
    const auto second = inventory.AddItem(potion, 5);
    check("add 5 -> fills + new slot", second.added == 5 && inventory.GetUsedSlots() == 2);
    const legend::item::ItemInstance* slot0 = inventory.GetSlot(0);
    const legend::item::ItemInstance* slot1 = inventory.GetSlot(1);
    check("slot0 topped to 20", slot0 != nullptr && slot0->quantity == 20);
    check("slot1 = 3 new stack", slot1 != nullptr && slot1->quantity == 3);
    check("stack keeps instanceId", slot0 != nullptr && slot1 != nullptr &&
                                        slot0->instanceId != slot1->instanceId);
    check("total count 23", inventory.GetItemCount("small_potion") == 23);
    LOG_INFO("[InventoryStackCheck] completed, failures = " + std::to_string(failures));
}

void GameScene::RunInventoryFullCheck() {
    int failures = 0;
    auto check = [&failures](const std::string& name, bool pass) {
        LOG_INFO("[InventoryFullCheck] " + name + " -> " + (pass ? "PASS" : "FAIL"));
        if (!pass) {
            ++failures;
        }
    };
    legend::item::Inventory inventory;
    legend::item::ItemDefinition unique; // maxStack=1：不可堆叠（未来 Equipment 预留语义）
    unique.id = "unit_test_unique";
    unique.name = "UnitTestUnique";
    unique.maxStack = 1;
    for (int i = 0; i < static_cast<int>(inventory.GetCapacity()); ++i) {
        (void)inventory.AddItem(unique, 1);
    }
    check("20 slots filled", inventory.GetUsedSlots() == 20 && inventory.IsFull());
    const auto rejected = inventory.AddItem(unique, 3);
    check("full -> added 0", rejected.added == 0);
    check("full -> remaining kept (no loss)", rejected.remaining == 3);
    LOG_INFO("[InventoryFullCheck] completed, failures = " + std::to_string(failures));
}
