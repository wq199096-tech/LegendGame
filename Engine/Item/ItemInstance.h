#pragma once

#include <cstdint>
#include <string>

namespace legend::item {

// 物品实例 ID：全局递增（与 EntityId / LootEntityId 互不混用）
using ItemInstanceId = std::uint64_t;

// 物品实例：一个 Stack 一条记录（同 definitionId 可共享 Stack）。
// 阶段6 不要：随机词条 / 品质 / 强化等级 / 耐久。
struct ItemInstance {
    ItemInstanceId instanceId = 0;
    std::string definitionId;
    int quantity = 0;
};

// 堆叠进已有 Stack：保留已有 instanceId；溢出新 Stack：新 instanceId
class ItemInstanceIdAllocator {
public:
    static ItemInstanceId Next() {
        return ++m_next;
    }
    static void Reset(ItemInstanceId start = 1) {
        m_next = start > 0 ? start : 1;
    }

private:
    inline static ItemInstanceId m_next = 0; // 0 保留（无效实例）
};

} // namespace legend::item
