#include "Server/WorldServer/Npc/ShopService.h"

#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

const ShopRegistry& ShopRegistry::Instance() {
    static const ShopRegistry registry;
    return registry;
}

ShopRegistry::ShopRegistry() {
    // 阶段23 23.22：构造即填充出厂默认（直接填充——见 QuestRegistry 构造注释，
    // 经 Mutable()/Instance() 会在 MSVC magic-static 初始化中重入死锁）。
    m_shops = MakeDefaultGameData().shops;
}

const ShopDefinition* ShopRegistry::FindShop(std::uint32_t shopId) const {
    for (const auto& shop : m_shops) {
        if (shop.shopId == shopId) {
            return &shop;
        }
    }
    return nullptr;
}

ShopRegistry& ShopRegistry::Mutable() {
    return const_cast<ShopRegistry&>(Instance());
}

void ShopRegistry::LoadFromDefinitions(std::vector<ShopDefinition> shops) {
    Mutable().m_shops = std::move(shops);
}

void ShopRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().shops);
}

} // namespace legend::world
