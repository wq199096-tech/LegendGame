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
// ID 分配器 Reset 语义（阶段6.1统一）：Reset(nextValue) —— 重置后下一次 Next() 返回 nextValue
// 本身（即 nextValue 视为"尚未分配"）。Reset(1) -> Next() == 1 -> 2 -> 3 ...；默认起点 1。
class ItemInstanceIdAllocator {
public:
    static ItemInstanceId Next() {
        return ++m_next;
    }
    // nextValue < 1 时按 1 处理（0 保留为无效实例）
    static void Reset(ItemInstanceId nextValue = 1) {
        m_next = nextValue > 0 ? nextValue - 1 : 0;
    }

private:
    inline static ItemInstanceId m_next = 0; // 0 保留（无效实例）
};

} // namespace legend::item
