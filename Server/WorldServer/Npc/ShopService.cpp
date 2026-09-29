#include "Server/WorldServer/Npc/ShopService.h"

namespace legend::world {

const ShopRegistry& ShopRegistry::Instance() {
    static const ShopRegistry registry;
    return registry;
}

ShopRegistry::ShopRegistry() {
    // 指令三十六：Shop 6001（General Merchant）——Slime Core / Rusty Sword / Cloth Armor。
    ShopDefinition shop;
    shop.shopId = 6001;
    shop.name = "General Merchant";
    shop.entries.push_back({kItemSlimeCoreId, 10, 3, true, true});
    shop.entries.push_back({kItemRustySwordId, 100, 30, true, true});
    shop.entries.push_back({kItemClothArmorId, 120, 40, true, true});
    m_shops.push_back(std::move(shop));
}

const ShopDefinition* ShopRegistry::FindShop(std::uint32_t shopId) const {
    for (const auto& shop : m_shops) {
        if (shop.shopId == shopId) {
            return &shop;
        }
    }
    return nullptr;
}

} // namespace legend::world
