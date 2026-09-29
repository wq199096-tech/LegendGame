#pragma once

#include "Shared/Item/ItemDefinition.h"

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18：ItemRegistry —— 静态物品定义注册表（硬编码三种，指令三）。
// 只读；World io 线程与测试线程并发读安全（无状态）。
// ---------------------------------------------------------------------------
class ItemRegistry {
public:
    ItemRegistry();

    std::size_t Count() const { return kItemCount; }
    const ItemDefinition* Find(std::uint32_t definitionId) const;
    const ItemDefinition* At(std::size_t index) const {
        return index < kItemCount ? &m_definitions[index] : nullptr;
    }

private:
    static constexpr std::size_t kItemCount = 3;
    ItemDefinition m_definitions[kItemCount];
};

} // namespace legend::world
