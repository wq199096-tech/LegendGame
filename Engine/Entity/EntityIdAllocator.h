#pragma once

#include <atomic>

#include "Engine/Entity/EntityId.h"

namespace legend::entity {

// 全局唯一 EntityId 分配器：统一递增（推荐方案），
// 保证场景内 Player / NPC / Monster 的 ID 永不冲突。
// kInvalidEntityId(0) 永不分配；进程内原子递增，线程安全。
class EntityIdAllocator {
public:
    static EntityId Next() {
        return m_next.fetch_add(1, std::memory_order_relaxed);
    }

    // 测试用：重置分配起点
    static void Reset(EntityId start = 1) {
        m_next.store(start < kInvalidEntityId + 1 ? kInvalidEntityId + 1 : start,
                     std::memory_order_relaxed);
    }

private:
    inline static std::atomic<EntityId> m_next{kInvalidEntityId + 1};
};

} // namespace legend::entity
