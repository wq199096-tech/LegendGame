#include "Client/World/WorldActorManager.h"

#include <cmath>
#include <cstdlib>
#include <ctime>

#include "Client/Character/PlayerCharacter.h"
#include "Client/World/MonsterCharacter.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/EntityIdAllocator.h"
#include "Engine/Map/Map.h"
#include "Engine/Map/MapRenderer.h"
#include "Engine/Resource/ResourceManager.h"

namespace legend::world {

namespace {
constexpr float kRenderMargin = 192.0f;      // 视口外扩
constexpr float kCorpseDelaySeconds = 1.5f;  // 死亡动画播完后尸体滞留时间

entity::Direction8 ParseDirection(const std::string& name) {
    for (uint8_t i = 0; i < 8; ++i) {
        const auto direction = static_cast<entity::Direction8>(i);
        if (name == entity::Direction8Name(direction)) {
            return direction;
        }
    }
    LOG_WARN("WorldActorManager: unknown direction '" + name + "', fallback south.");
    return entity::Direction8::South;
}
} // namespace

bool WorldActorManager::Initialize(legend::resource::ResourceManager& resources) {
    m_resources = &resources;
    m_assetsRoot = resources.GetAssetRoot();
    if (!m_spawner.Initialize(m_assetsRoot, resources)) {
        LOG_ERROR("WorldActorManager: monster spawner initialize failed.");
        return false;
    }
    if (const char* seedEnv = std::getenv("LEGEND_AI_SEED"); seedEnv != nullptr && seedEnv[0] != '\0') {
        const unsigned long seed = std::strtoul(seedEnv, nullptr, 10);
        m_rng.seed(seed);
        LOG_INFO("WorldActorManager: AI seed from LEGEND_AI_SEED = " + std::to_string(seed));
    } else {
        m_rng.seed(static_cast<unsigned long>(time(nullptr)));
    }
    // ---- 阶段6：物品库（启动加载一次，失败禁止继续——否则空库会把全部 Monster Loot 剔除
    //      而游戏仍声称初始化成功） + 掉落管理器（LEGEND_LOOT_SEED 支持可重复测试） ----
    if (!m_itemDatabase.LoadFromFile(m_assetsRoot + "/Items/items.json")) {
        LOG_ERROR("WorldActorManager: item database load failed, abort initialize.");
        return false;
    }
    unsigned int lootSeed = 20260927u; // 固定默认：掉落结果可复现
    if (const char* lootSeedEnv = std::getenv("LEGEND_LOOT_SEED");
        lootSeedEnv != nullptr && lootSeedEnv[0] != '\0') {
        lootSeed = static_cast<unsigned int>(std::strtoul(lootSeedEnv, nullptr, 10));
    } else if (const char* aiSeedEnv = std::getenv("LEGEND_AI_SEED");
               aiSeedEnv != nullptr && aiSeedEnv[0] != '\0') {
        lootSeed = static_cast<unsigned int>(std::strtoul(aiSeedEnv, nullptr, 10));
    }
    m_loot.Initialize(lootSeed, &m_itemDatabase);
    // 掉落表 item 存在性校验：指向不存在 Item 的 entry 剔除（不生成未知物品）
    m_spawner.ValidateLootEntries(m_itemDatabase);
    return true;
}

void WorldActorManager::Shutdown() {
    m_loot.Shutdown();
    m_registry.Clear();
    m_aiControllers.clear();
    m_ownedActors.clear();
    m_npcIds.clear();
    m_monsterIds.clear();
    m_despawnTimers.clear();
    m_respawnQueue.clear();
    m_spawnAreas.clear();
}

void WorldActorManager::RegisterPlayer(PlayerCharacter* player) {
    m_player = player;      // 奖励归属（killer=Player 判定 + Exp/Inventory 入口）
    m_registry.Register(player);
}

const WorldActorManager::NPCTemplate* WorldActorManager::LoadNPCTemplate(
    const std::string& characterPath) {
    const auto it = m_npcTemplates.find(characterPath);
    if (it != m_npcTemplates.end()) {
        return &it->second;
    }
    legend::animation::CharacterDefinition definition;
    if (!legend::animation::LoadCharacterDefinition(m_assetsRoot + "/" + characterPath,
                                                    definition)) {
        return nullptr;
    }
    NPCTemplate npcTemplate;
    npcTemplate.definition = definition;
    npcTemplate.clips =
        std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>(
            legend::animation::LoadAnimationClips(m_assetsRoot + "/" +
                                                  definition.animationsPath));
    npcTemplate.sheet = legend::animation::LoadSpriteSheet(definition, *m_resources);
    if (npcTemplate.clips->empty() || !npcTemplate.sheet || !npcTemplate.sheet->IsValid()) {
        LOG_ERROR("WorldActorManager: NPC template asset load failed: " + characterPath);
        return nullptr;
    }
    auto [inserted, ok] = m_npcTemplates.emplace(characterPath, std::move(npcTemplate));
    (void)ok;
    return &inserted->second;
}

int WorldActorManager::SpawnNPCs(const map::Map& map) {
    int spawned = 0;
    for (const map::MapNPCSpawn& spawn : map.GetNPCSpawns()) {
        const NPCTemplate* npcTemplate = LoadNPCTemplate(spawn.characterPath);
        if (npcTemplate == nullptr) {
            LOG_ERROR("WorldActorManager: NPC '" + spawn.name + "' template load failed.");
            continue;
        }
        auto npc = std::make_unique<NPCCharacter>(legend::entity::EntityIdAllocator::Next(),
                                                  spawn.name, npcTemplate->definition,
                                                  npcTemplate->clips, npcTemplate->sheet,
                                                  ParseDirection(spawn.direction));
        npc->SetPosition({spawn.x, spawn.y});
        // NPC 不参与战斗（CombatEnabled=false 默认）：不可被攻击/锁定
        m_registry.Register(npc.get());
        m_npcIds.push_back(npc->GetId());
        m_ownedActors.push_back(std::move(npc));
        ++spawned;
        LOG_INFO("WorldActorManager: NPC '" + spawn.name + "' spawned at (" +
                 std::to_string(spawn.x) + "," + std::to_string(spawn.y) + "), facing " +
                 spawn.direction + ".");
    }
    return spawned;
}

MonsterCharacter* WorldActorManager::SpawnSingleMonster(const std::string& templateId,
                                                        const map::MapSpawnArea& area,
                                                        const map::Map& map) {
    auto monster = m_spawner.SpawnSingle(templateId, area, map, m_rng);
    if (monster == nullptr) {
        return nullptr;
    }
    monster->SetCombatEnabled(true);
    const auto* def = m_spawner.GetDefinition(templateId);
    if (def != nullptr) {
        monster->GetCombatStats() = def->combat; // 数据驱动战斗属性
    }
    m_registry.Register(monster.get());
    m_monsterIds.push_back(monster->GetId());
    m_aiControllers.emplace(monster->GetId(), MonsterAIController{});
    MonsterCharacter* raw = monster.get();
    m_ownedActors.push_back(std::move(monster));
    return raw;
}

WorldSpawnStats WorldActorManager::SpawnMonsters(const map::Map& map) {
    WorldSpawnStats stats;
    m_spawnAreas = map.GetMonsterSpawns(); // 副本供重生查询
    for (const map::MapSpawnArea& area : map.GetMonsterSpawns()) {
        stats.requested += area.count;
        auto monsters = m_spawner.SpawnArea(area, map, m_rng);
        for (auto& monster : monsters) {
            monster->SetCombatEnabled(true);
            const auto* def = m_spawner.GetDefinition(area.monsterId);
            if (def != nullptr) {
                monster->GetCombatStats() = def->combat; // 数据驱动战斗属性
            }
            m_registry.Register(monster.get());
            m_monsterIds.push_back(monster->GetId());
            m_aiControllers.emplace(monster->GetId(), MonsterAIController{});
            m_ownedActors.push_back(std::move(monster));
            ++stats.spawned;
        }
        stats.failed += area.count - static_cast<int>(monsters.size());
    }
    LOG_INFO("WorldActorManager: monsters requested " + std::to_string(stats.requested) +
             ", spawned " + std::to_string(stats.spawned) + ", failed " +
             std::to_string(stats.failed) + ".");
    return stats;
}

void WorldActorManager::Update(const map::Map& map, float deltaTime) {
    // 1. NPC 动画 + Monster AI/战斗 + 动画（统一顺序）
    for (entity::Character* actor : m_registry.GetAll()) {
        if (actor == nullptr || !actor->IsActive()) {
            continue;
        }
        switch (actor->GetActorType()) {
        case entity::ActorType::NPC: {
            actor->UpdateAnimation(deltaTime);
            break;
        }
        case entity::ActorType::Monster: {
            const auto it = m_aiControllers.find(actor->GetId());
            if (it == m_aiControllers.end()) {
                break;
            }
            it->second.Update(*static_cast<MonsterCharacter*>(actor), map, m_registry,
                              m_characterController, m_combat, m_rng, deltaTime);
            actor->UpdateAnimation(deltaTime);
            break;
        }
        default:
            break; // Player 由 GameScene 更新
        }
    }

    // 2. 伤害事件分发：受击怪物 AddThreat(sourceId, damage)（被打必反击）
    for (const auto& event : m_combat.GetRecentEvents()) {
        entity::Character* victim = m_registry.Get(event.targetId);
        if (victim == nullptr || victim->GetActorType() != entity::ActorType::Monster) {
            continue;
        }
        const auto aiIt = m_aiControllers.find(event.targetId);
        if (aiIt != m_aiControllers.end()) {
            aiIt->second.OnDamaged(*static_cast<MonsterCharacter*>(victim), event.sourceId,
                                   event.finalDamage);
        }
    }
    m_combat.ClearRecentEvents();

    // 2.5 阶段6：死亡奖励分发（Exp + Loot）——DeathEvent 一次消费，奖励与 Respawn 分离
    m_rewards.ProcessDeathEvents(m_combat, m_registry, m_player, m_loot, m_spawner,
                                 m_itemDatabase);

    // 3. 死亡收集：Dead + 死亡动画播完 -> Corpse 滞留 1.5s -> Despawn
    for (entity::EntityId id : m_monsterIds) {
        entity::Character* actor = m_registry.Get(id);
        if (actor == nullptr || actor->GetActionState() != entity::CharacterActionState::Dead ||
            !actor->GetAnimationPlayer().IsFinished()) {
            continue;
        }
        const auto pending = m_despawnTimers.find(id);
        if (pending == m_despawnTimers.end()) {
            m_despawnTimers.emplace(id, kCorpseDelaySeconds);
        }
    }

    // 4. Despawn：清 AI -> Registry 注销 -> 所有权释放 -> 登记重生
    std::vector<entity::EntityId> despawned;
    for (auto& [id, timer] : m_despawnTimers) {
        timer -= deltaTime;
        if (timer <= 0.0f) {
            despawned.push_back(id);
        }
    }
    for (entity::EntityId id : despawned) {
        // 找到模板与区域信息（重生用）
        std::string templateId;
        uint32_t areaId = 0;
        float respawnSeconds = 5.0f;
        for (entity::Character* actor : m_registry.GetAll()) {
            if (actor->GetId() == id && actor->GetActorType() == entity::ActorType::Monster) {
                templateId = static_cast<MonsterCharacter*>(actor)->GetMonsterTemplateId();
                areaId = static_cast<MonsterCharacter*>(actor)->GetSpawnAreaId();
                break;
            }
        }
        for (const auto& area : m_spawnAreas) {
            if (area.id == areaId) {
                respawnSeconds = area.respawnSeconds;
                break;
            }
        }
        DespawnMonster(id);
        m_despawnTimers.erase(id);
        if (!templateId.empty()) {
            m_respawnQueue.push_back({areaId, templateId, respawnSeconds});
            LOG_INFO("[Respawn] queued " + templateId + " in area " + std::to_string(areaId) +
                     " after " + std::to_string(respawnSeconds) + "s.");
        }
    }

    // 5. Respawn：时间到在原区域重新生成（新 EntityId，数量守恒）
    for (auto& request : m_respawnQueue) {
        request.timer -= deltaTime;
        if (request.timer > 0.0f) {
            continue;
        }
        const map::MapSpawnArea* area = nullptr;
        for (const auto& areaData : m_spawnAreas) {
            if (areaData.id == request.areaId) {
                area = &areaData;
                break;
            }
        }
        if (area != nullptr) {
            auto* monster = SpawnSingleMonster(request.templateId, *area, map);
            if (monster != nullptr) {
                LOG_INFO("[Respawn] " + request.templateId + " new entity #" +
                         std::to_string(monster->GetId()) + " in area " +
                         std::to_string(request.areaId));
                request.timer = -1.0f; // 标记完成
            } else {
                request.timer = 1.0f; // 位置全被占：稍后重试
            }
        } else {
            request.timer = -1.0f;
        }
    }
    m_respawnQueue.erase(std::remove_if(m_respawnQueue.begin(), m_respawnQueue.end(),
                                        [](const RespawnRequest& r) { return r.timer < 0.0f; }),
                         m_respawnQueue.end());

    // 6. 扫描计数统计
    m_totalScanCount = 0;
    for (const auto& [id, controller] : m_aiControllers) {
        m_totalScanCount += controller.GetScanCount();
    }
}

void WorldActorManager::DespawnMonster(entity::EntityId id) {
    // 移除顺序（禁止先 delete 再留野指针）：
    // 1) Registry 注销（所有 TargetHandle 自动失效） 2) AI controller 3) monsterIds 4) 所有权
    m_registry.Unregister(id);
    m_aiControllers.erase(id);
    m_monsterIds.erase(std::remove(m_monsterIds.begin(), m_monsterIds.end(), id),
                       m_monsterIds.end());
    m_ownedActors.erase(std::remove_if(m_ownedActors.begin(), m_ownedActors.end(),
                                       [id](const std::unique_ptr<entity::Character>& owned) {
                                           return owned != nullptr && owned->GetId() == id;
                                       }),
                        m_ownedActors.end());
}

int WorldActorManager::GetAliveMonsterCount() const {
    int alive = 0;
    for (entity::EntityId id : m_monsterIds) {
        const entity::Character* actor = m_registry.Get(id);
        if (actor != nullptr && actor->IsCombatAlive()) {
            ++alive;
        }
    }
    return alive;
}

int WorldActorManager::GetAreaAliveCount(uint32_t areaId) const {
    int alive = 0;
    for (entity::EntityId id : m_monsterIds) {
        const entity::Character* actor = m_registry.Get(id);
        if (actor != nullptr && actor->IsCombatAlive()) {
            const auto* monster = static_cast<const MonsterCharacter*>(actor);
            if (monster->GetSpawnAreaId() == areaId) {
                ++alive;
            }
        }
    }
    return alive;
}

void WorldActorManager::CollectRenderItems(std::vector<map::RenderSortItem>& items,
                                           float viewLeft, float viewRight, float viewTop,
                                           float viewBottom) const {
    for (entity::Character* actor : m_registry.GetAll()) {
        // 阶段5：inactive 不入渲染队列（死亡动画期间 active=true，仍正常渲染）
        if (actor == nullptr || !actor->IsActive() || !actor->IsVisible()) {
            continue;
        }
        const auto& visual = actor->GetVisual();
        const math::Vector2& feet = actor->GetPosition();
        const bool visible = feet.x + visual.width >= viewLeft - kRenderMargin &&
                             feet.x - visual.width <= viewRight + kRenderMargin &&
                             feet.y + visual.height >= viewTop - kRenderMargin &&
                             feet.y - visual.height <= viewBottom + kRenderMargin;
        if (!visible) {
            continue;
        }
        items.push_back({0, feet.y, 10, legend::map::RenderSortItem::Type::Character, nullptr,
                         actor});
    }
}

int WorldActorManager::ProcessDeathRewards() {
    // 阶段6：手动触发奖励分发（自动测试 [DeathRewardCheck] 用；Update 内每帧同样调用）
    return m_rewards.ProcessDeathEvents(m_combat, m_registry, m_player, m_loot, m_spawner,
                                        m_itemDatabase);
}

std::vector<MonsterCharacter*> WorldActorManager::GetMonsters() const {
    std::vector<MonsterCharacter*> monsters;
    monsters.reserve(m_monsterIds.size());
    for (const entity::EntityId id : m_monsterIds) {
        entity::Character* actor = m_registry.Get(id);
        if (actor != nullptr) {
            monsters.push_back(static_cast<MonsterCharacter*>(actor));
        }
    }
    return monsters;
}

} // namespace legend::world