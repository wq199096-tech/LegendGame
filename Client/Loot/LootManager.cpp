#include "Client/Loot/LootManager.h"

#include <algorithm>
#include <cstdlib>

#include "Engine/Debug/Logger.h"
#include "Engine/Item/ItemDatabase.h"

namespace legend::world {

namespace {
constexpr float kDefaultTtlSeconds = 60.0f;   // 掉落存在 60 秒
constexpr float kRenderMargin = 96.0f;        // 视口外扩
} // namespace

void LootManager::Initialize(unsigned int seed, const item::ItemDatabase* items) {
    m_rng.seed(seed);
    m_items = items;
    m_ttlSeconds = kDefaultTtlSeconds;
    if (const char* ttlEnv = std::getenv("LEGEND_LOOT_TTL"); ttlEnv != nullptr && ttlEnv[0] != '\0') {
        const float ttl = std::strtof(ttlEnv, nullptr);
        if (ttl > 0.0f) {
            m_ttlSeconds = ttl; // 自动测试环境可配更长/更短
        }
    }
    LOG_INFO("LootManager initialized (seed " + std::to_string(seed) + ", ttl " +
             std::to_string(m_ttlSeconds) + "s).");
}

void LootManager::SpawnMonsterLoot(const MonsterDefinition& definition,
                                   const math::Vector2& deathPos) {
    if (definition.loot.empty()) {
        return;
    }
    for (const LootRollResult& roll : RollLootTable(definition.loot, m_rng)) {
        if (m_items != nullptr && !m_items->Exists(roll.itemId)) {
            LOG_WARN("LootManager: unknown item '" + roll.itemId + "' in loot table of '" +
                     definition.id + "', entry skipped.");
            continue; // 不生成未知物品
        }
        SpawnGroundLoot(roll.itemId, roll.quantity, deathPos);
    }
}

GroundLoot* LootManager::SpawnGroundLoot(const std::string& itemId, int quantity,
                                         const math::Vector2& position) {
    if (itemId.empty() || quantity <= 0) {
        return nullptr;
    }
    GroundLoot loot;
    loot.lootEntityId = LootEntityIdAllocator::Next();
    loot.itemId = itemId;
    loot.quantity = quantity;
    loot.position = position;
    loot.age = 0.0f;
    loot.pickupEnabled = true;
    m_loots.push_back(std::move(loot));
    ++m_totalSpawned;
    return &m_loots.back();
}

void LootManager::Update(float deltaTime) {
    for (GroundLoot& loot : m_loots) {
        loot.age += deltaTime; // 屏幕外也继续计时
    }
    const std::size_t before = m_loots.size();
    m_loots.erase(std::remove_if(m_loots.begin(), m_loots.end(),
                                 [this](const GroundLoot& loot) {
                                     return loot.age >= m_ttlSeconds;
                                 }),
                  m_loots.end());
    m_totalExpired += static_cast<std::uint64_t>(before - m_loots.size());
}

int LootManager::PickupNearest(const math::Vector2& playerPos, float radius,
                               item::Inventory& inventory, const item::ItemDatabase& items) {
    const GroundLoot* nearest = FindNearestPickup(playerPos, radius);
    if (nearest == nullptr) {
        return 0;
    }
    const item::ItemDefinition* definition = items.Get(nearest->itemId);
    if (definition == nullptr) {
        LOG_WARN("LootManager: cannot pickup unknown item '" + nearest->itemId + "'.");
        return 0;
    }
    // GroundLoot 在 m_loots 中的位置（FindNearestPickup 保证非空）
    GroundLoot& loot = const_cast<GroundLoot&>(*nearest);
    const std::string itemId = loot.itemId; // erase 后 nearest 悬空，先拷贝用于日志
    const item::InventoryAddResult result = inventory.AddItem(*definition, loot.quantity);
    if (result.added <= 0) {
        LOG_WARN("[Inventory] Inventory full."); // 不删除 GroundLoot
        return 0;
    }
    if (result.added >= loot.quantity) {
        // 全部拾取：删除地上物品
        const auto it = std::find_if(m_loots.begin(), m_loots.end(), [&loot](const GroundLoot& l) {
            return l.lootEntityId == loot.lootEntityId;
        });
        if (it != m_loots.end()) {
            m_loots.erase(it);
        }
    } else {
        loot.quantity -= result.added; // 部分拾取：剩余保留地上
    }
    LOG_INFO("[Pickup] " + itemId + " x" + std::to_string(result.added) +
             " (remaining on ground: " + std::to_string(result.remaining) + ")");
    return result.added;
}

const GroundLoot* LootManager::FindNearestPickup(const math::Vector2& playerPos,
                                                 float radius) const {
    const GroundLoot* best = nullptr;
    float bestDistSq = radius * radius;
    for (const GroundLoot& loot : m_loots) {
        if (!loot.pickupEnabled) {
            continue;
        }
        const float distSq = (loot.position - playerPos).LengthSq();
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            best = &loot;
        }
    }
    return best;
}

void LootManager::CollectRenderItems(std::vector<map::RenderSortItem>& items, float viewLeft,
                                     float viewRight, float viewTop, float viewBottom) const {
    for (const GroundLoot& loot : m_loots) {
        // 视口剔除（屏幕外不渲染，但 TTL 计时照常）
        if (loot.position.x < viewLeft - kRenderMargin || loot.position.x > viewRight + kRenderMargin ||
            loot.position.y < viewTop - kRenderMargin || loot.position.y > viewBottom + kRenderMargin) {
            continue;
        }
        map::RenderSortItem item;
        item.sortLayer = 0;
        item.sortY = loot.position.y; // 与角色同一 Y-Sort 规则，不单独画顶层
        item.renderOrder = 20;
        item.type = map::RenderSortItem::Type::GroundLoot;
        item.groundLoot = &loot;
        items.push_back(item);
    }
}

} // namespace legend::world
