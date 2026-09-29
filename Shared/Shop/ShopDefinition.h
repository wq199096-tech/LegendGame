#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令三十六/三十七：Shop 静态定义（价格 100% 服务器权威——指令三十八；
// 无限库存不存数量——指令八十三）。代码硬编码，不入数据库（指令一百零八）。
// ---------------------------------------------------------------------------
struct ShopEntry {
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t buyPrice = 0;
    std::uint32_t sellPrice = 0;
    bool canBuy = true;
    bool canSell = true;
};

struct ShopDefinition {
    std::uint32_t shopId = 0;
    std::string name;
    std::vector<ShopEntry> entries;
    const ShopEntry* FindEntry(std::uint32_t itemDefinitionId) const {
        for (const auto& entry : entries) {
            if (entry.itemDefinitionId == itemDefinitionId) {
                return &entry;
            }
        }
        return nullptr;
    }
};

} // namespace legend::world
