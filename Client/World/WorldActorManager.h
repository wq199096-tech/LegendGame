#pragma once

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "Client/World/MonsterAIController.h"
#include "Client/World/MonsterSpawner.h"
#include "Client/World/NPCCharacter.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/CharacterController.h"
#include "Engine/Map/MapTypes.h"

namespace legend::map {
class Map;
struct RenderSortItem;
}

namespace legend::resource {
class ResourceManager;
}

namespace legend::world {

// 世界角色统一管理器：
// - 拥有 NPC / Monster 生命周期（unique_ptr），Player 外部注册
// - ActorRegistry 统一引用；统一更新（NPC 动画 / Monster AI + 战斗 + 动画）
// - 统一渲染收集；死亡动画 -> Corpse 延迟 -> Despawn -> Respawn（count 守恒）
// - 战斗规则在 CombatSystem（本类不写伤害公式）
struct WorldSpawnStats {
    int requested = 0;
    int spawned = 0;
    int failed = 0;
};

// 重生请求：怪物 Despawn 后登记，时间到在原 SpawnArea 重生（新 EntityId）
struct RespawnRequest {
    uint32_t areaId = 0;
    std::string templateId;
    float timer = 0.0f; // 剩余秒数
};

class WorldActorManager {
public:
    bool Initialize(legend::resource::ResourceManager& resources);
    void Shutdown();

    void RegisterPlayer(entity::Character* player);

    int SpawnNPCs(const map::Map& map);
    WorldSpawnStats SpawnMonsters(const map::Map& map);

    // 统一更新：事件分发 -> AI/战斗 -> 动画 -> 死亡收集 -> Despawn -> Respawn
    void Update(const map::Map& map, float deltaTime);

    void CollectRenderItems(std::vector<map::RenderSortItem>& items, float viewLeft,
                            float viewRight, float viewTop, float viewBottom) const;

    entity::ActorRegistry& GetRegistry() { return m_registry; }
    const entity::ActorRegistry& GetRegistry() const { return m_registry; }
    MonsterSpawner& GetSpawner() { return m_spawner; }
    combat::CombatSystem& GetCombatSystem() { return m_combat; }

    int GetMonsterCount() const { return static_cast<int>(m_monsterIds.size()); }
    int GetNPCCount() const { return static_cast<int>(m_npcIds.size()); }
    int GetAliveMonsterCount() const; // active + 战斗存活数（RespawnCheck 用）
    int GetAreaAliveCount(uint32_t areaId) const;
    uint64_t GetTotalScanCount() const { return m_totalScanCount; }
    const std::mt19937& GetRng() const { return m_rng; }

    std::vector<MonsterCharacter*> GetMonsters() const;
    const MonsterAIController* GetAIController(entity::EntityId id) const {
        const auto it = m_aiControllers.find(id);
        return it != m_aiControllers.end() ? &it->second : nullptr;
    }

private:
    struct NPCTemplate {
        legend::animation::CharacterDefinition definition;
        std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips;
        std::shared_ptr<legend::animation::SpriteSheet> sheet;
    };
    const NPCTemplate* LoadNPCTemplate(const std::string& characterPath);
    // 在指定区域生成一只怪（重生用：新 EntityId，count 守恒由死亡触发保证）
    MonsterCharacter* SpawnSingleMonster(const std::string& templateId, const map::MapSpawnArea& area,
                                         const map::Map& map);
    void DespawnMonster(entity::EntityId id);

    entity::ActorRegistry m_registry;
    combat::CombatSystem m_combat{m_registry}; // 依赖 m_registry 先初始化
    MonsterSpawner m_spawner;
    entity::CharacterController m_characterController;

    std::vector<std::unique_ptr<entity::Character>> m_ownedActors;
    std::vector<entity::EntityId> m_npcIds;
    std::vector<entity::EntityId> m_monsterIds;
    std::unordered_map<entity::EntityId, MonsterAIController> m_aiControllers;
    std::unordered_map<std::string, NPCTemplate> m_npcTemplates;

    // 死亡 -> Corpse 延迟 -> Despawn -> Respawn
    std::unordered_map<entity::EntityId, float> m_despawnTimers; // 死亡动画播完后的滞留秒数
    std::vector<RespawnRequest> m_respawnQueue;
    std::vector<map::MapSpawnArea> m_spawnAreas; // 地图出生区域副本（重生查询）

    std::mt19937 m_rng{12345};
    uint64_t m_totalScanCount = 0;
    std::string m_assetsRoot;
    legend::resource::ResourceManager* m_resources = nullptr;
};

} // namespace legend::world