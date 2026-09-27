#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "Client/Loot/GroundLoot.h"
#include "Client/Loot/LootTable.h"
#include "Client/World/MonsterDefinition.h"
#include "Engine/Item/Inventory.h"
#include "Engine/Map/MapRenderer.h"

namespace legend::item {
class ItemDatabase;
}

namespace legend::world {

// 掉落管理器：ground loot 的 ownership / spawn / update / expire / pickup / render collect。
// GameScene 不直接持有 vector<GroundLoot>。
class LootManager {
public:
    // seed 支持 LEGEND_LOOT_SEED（自动测试可重复）；items 用于掉落 item 存在性校验
    void Initialize(unsigned int seed, const item::ItemDatabase* items);
    void Shutdown() { m_loots.clear(); }

    // 怪物死亡掉落：RollLootTable -> 每个结果生成一件 GroundLoot（位置 = 死亡 Feet）。
    // 立即产生（不等尸体 Despawn）。指向不存在 Item 的 entry 跳过（log，不崩）。
    void SpawnMonsterLoot(const MonsterDefinition& definition, const math::Vector2& deathPos);

    // 直接生成（自动测试/调试用）：返回生成的 loot 指针（owner 不变）
    GroundLoot* SpawnGroundLoot(const std::string& itemId, int quantity,
                                const math::Vector2& position);

    // TTL 过期（默认 60s，LEGEND_LOOT_TTL 覆盖；自动测试环境可配更长）
    void Update(float deltaTime);

    // E 拾取：范围内最近一件；全部加入 -> 删除；部分加入 -> quantity 减少剩余；
    // 0 加入（背包满）-> 保留地上 + [Inventory] Inventory full。返回实际拾取数。
    int PickupNearest(const math::Vector2& playerPos, float radius, item::Inventory& inventory,
                      const item::ItemDatabase& items);

    // 统一 World Render Queue（sortY = position.y；视口剔除、过期计时不受影响）
    void CollectRenderItems(std::vector<map::RenderSortItem>& items, float viewLeft,
                            float viewRight, float viewTop, float viewBottom) const;

    // 范围内最近可拾取（E 提示 / F5 Debug）；无返回 nullptr
    const GroundLoot* FindNearestPickup(const math::Vector2& playerPos, float radius) const;

    // 阶段8.1：按 lootEntityId 移除地上掉落（Skill Check / Auto Test 清理测试新增
    // GroundLoot 专用，普通游戏不调用）；不存在返回 false
    bool RemoveById(LootEntityId id);

    std::size_t GetCount() const { return m_loots.size(); }
    const std::vector<GroundLoot>& GetAll() const { return m_loots; }
    std::uint64_t GetTotalSpawned() const { return m_totalSpawned; } // 测试统计（含直接生成）
    std::uint64_t GetTotalExpired() const { return m_totalExpired; }

private:
    std::vector<GroundLoot> m_loots;
    std::mt19937 m_rng{12345};
    const item::ItemDatabase* m_items = nullptr;
    float m_ttlSeconds = 60.0f;
    std::uint64_t m_totalSpawned = 0;
    std::uint64_t m_totalExpired = 0;
};

} // namespace legend::world
