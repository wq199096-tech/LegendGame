#pragma once

#include "Shared/Item/ItemTypes.h"
#include "Shared/Shop/ShopDefinition.h"
#include "Shared/Shop/ShopTypes.h"

#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令三十六：ShopRegistry —— 商店 6001 硬编码注册表（价格服务器权威，
// 无限库存）。只读单例；NpcRegistry 启动校验引用。
// ---------------------------------------------------------------------------
class ShopRegistry {
public:
    static const ShopRegistry& Instance();

    ShopRegistry();

    const ShopDefinition* FindShop(std::uint32_t shopId) const;

private:
    std::vector<ShopDefinition> m_shops;
};

} // namespace legend::world
