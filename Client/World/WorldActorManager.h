#pragma once

#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "Client/World/MonsterAIController.h"
#include "Client/World/MonsterSpawner.h"
#include "Client/World/NPCCharacter.h"
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

// 世界角色统一管理器（阶段4核心）：
// - 拥有 NPC / Monster 的生命周期（unique_ptr），Player 为外部注册（GameScene 持有）
// - ActorRegistry 统一管理引用，GameScene 不再散乱保存 vector<Player>/vector<Monster>
// - 统一 World Actor 更新（NPC 动画 / Monster AI + 动画）
// - 统一 World Render Queue 收集（视口剔除 + Y-Sort 项）
struct WorldSpawnStats {
    int requested = 0;
    int spawned = 0;
    int failed = 0;
};

class WorldActorManager {
public:
    // 初始化（MonsterSpawner 模板资源加载 + LEGEND_AI_SEED 随机种子）
    bool Initialize(legend::resource::ResourceManager& resources);
    void Shutdown();

    // Player 注册（非拥有引用；生命周期由 GameScene 管理）
    void RegisterPlayer(entity::Character* player);

    // 按地图数据生成 NPC（静态站立）与怪物；返回统计
    int SpawnNPCs(const map::Map& map);
    WorldSpawnStats SpawnMonsters(const map::Map& map);

    // 统一 World Actor 更新：NPC 动画状态机 + Monster AI + 动画状态机
    void Update(const map::Map& map, float deltaTime);

    // 收集世界角色渲染项（视口剔除，margin 192；sortY=Feet Y）
    void CollectRenderItems(std::vector<map::RenderSortItem>& items, float viewLeft,
                            float viewRight, float viewTop, float viewBottom) const;

    // ---- 访问与统计 ----
    entity::ActorRegistry& GetRegistry() { return m_registry; }
    const entity::ActorRegistry& GetRegistry() const { return m_registry; }
    MonsterSpawner& GetSpawner() { return m_spawner; }

    int GetMonsterCount() const { return static_cast<int>(m_monsterIds.size()); }
    int GetNPCCount() const { return static_cast<int>(m_npcIds.size()); }
    uint64_t GetTotalScanCount() const { return m_totalScanCount; }
    const std::mt19937& GetRng() const { return m_rng; }

    // 遍历全部 Monster（AI 测试 / F3 Debug 用）
    std::vector<MonsterCharacter*> GetMonsters() const;
    // 指定怪物的 AI 控制器（F3 Debug 读取 Wander Target / 扫描计数）
    const MonsterAIController* GetAIController(entity::EntityId id) const {
        const auto it = m_aiControllers.find(id);
        return it != m_aiControllers.end() ? &it->second : nullptr;
    }

private:
    // character.json 路径 -> 已加载资源（NPC 共享模板）
    struct NPCTemplate {
        legend::animation::CharacterDefinition definition;
        std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips;
        std::shared_ptr<legend::animation::SpriteSheet> sheet;
    };
    const NPCTemplate* LoadNPCTemplate(const std::string& characterPath);

    entity::ActorRegistry m_registry;
    MonsterSpawner m_spawner;
    entity::CharacterController m_characterController;

    // 所有权：NPC + Monster（Player 不在此）
    std::vector<std::unique_ptr<entity::Character>> m_ownedActors;
    std::vector<entity::EntityId> m_npcIds;
    std::vector<entity::EntityId> m_monsterIds;
    std::unordered_map<entity::EntityId, MonsterAIController> m_aiControllers;
    std::unordered_map<std::string, NPCTemplate> m_npcTemplates;

    std::mt19937 m_rng{12345};
    uint64_t m_totalScanCount = 0;
    std::string m_assetsRoot;
    legend::resource::ResourceManager* m_resources = nullptr;
};

} // namespace legend::world
