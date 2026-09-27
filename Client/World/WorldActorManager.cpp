#include "Client/World/WorldActorManager.h"

#include <cmath>
#include <cstdlib>
#include <ctime>

#include "Client/World/MonsterCharacter.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/EntityIdAllocator.h"
#include "Engine/Map/Map.h"
#include "Engine/Map/MapRenderer.h"
#include "Engine/Resource/ResourceManager.h"

namespace legend::world {

namespace {
constexpr float kRenderMargin = 192.0f; // 与玩家可见判定一致的视口外扩

// 方向字符串 -> Direction8（map.json npcSpawns.direction）
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
    // 随机种子：LEGEND_AI_SEED 可复现，缺省用时间
    if (const char* seedEnv = std::getenv("LEGEND_AI_SEED"); seedEnv != nullptr && seedEnv[0] != '\0') {
        const unsigned long seed = std::strtoul(seedEnv, nullptr, 10);
        m_rng.seed(seed);
        LOG_INFO("WorldActorManager: AI seed from LEGEND_AI_SEED = " + std::to_string(seed));
    } else {
        m_rng.seed(static_cast<unsigned long>(time(nullptr)));
    }
    return true;
}

void WorldActorManager::Shutdown() {
    m_registry.Clear();
    m_aiControllers.clear();
    m_ownedActors.clear();
    m_npcIds.clear();
    m_monsterIds.clear();
}

void WorldActorManager::RegisterPlayer(entity::Character* player) {
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

WorldSpawnStats WorldActorManager::SpawnMonsters(const map::Map& map) {
    WorldSpawnStats stats;
    for (const map::MapSpawnArea& area : map.GetMonsterSpawns()) {
        stats.requested += area.count;
        auto monsters = m_spawner.SpawnArea(area, map, m_rng);
        for (auto& monster : monsters) {
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
    for (entity::Character* actor : m_registry.GetAll()) {
        if (actor == nullptr || !actor->IsActive()) {
            continue;
        }
        switch (actor->GetActorType()) {
        case entity::ActorType::NPC: {
            // 静态站立：Idle Clip 逐帧循环播放（moving=false 保持 Idle + 固定朝向）
            actor->UpdateAnimation(deltaTime);
            break;
        }
        case entity::ActorType::Monster: {
            const auto it = m_aiControllers.find(actor->GetId());
            if (it == m_aiControllers.end()) {
                break;
            }
            it->second.Update(*static_cast<MonsterCharacter*>(actor), map, m_registry,
                              m_characterController, m_rng, deltaTime);
            // AI 决定 moving/direction 后：Idle 播 idle_，移动/Chase 播 walk_，
            // 撞墙未实际位移时 CharacterController 已置 moving=false -> 自动回落 idle_
            actor->UpdateAnimation(deltaTime);
            break;
        }
        default:
            break; // Player 由 GameScene 更新
        }
    }
    // 统计感知扫描总数（窗口标题 Scans）
    m_totalScanCount = 0;
    for (const auto& [id, controller] : m_aiControllers) {
        m_totalScanCount += controller.GetScanCount();
    }
}

void WorldActorManager::CollectRenderItems(std::vector<map::RenderSortItem>& items,
                                           float viewLeft, float viewRight, float viewTop,
                                           float viewBottom) const {
    for (entity::Character* actor : m_registry.GetAll()) {
        if (actor == nullptr || !actor->IsVisible()) {
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
