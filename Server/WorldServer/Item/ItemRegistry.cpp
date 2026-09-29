#include "Server/WorldServer/Item/ItemRegistry.h"

#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

ItemRegistry::ItemRegistry() {
    // 阶段23 23.22：构造即装载出厂默认（保持"构造后可用"语义）；Start 覆盖注入。
    LoadDefaults();
}

const ItemDefinition* ItemRegistry::Find(std::uint32_t definitionId) const {
    for (const auto& def : m_definitions) {
        if (def.definitionId == definitionId) {
            return &def;
        }
    }
    return nullptr;
}

void ItemRegistry::LoadFromDefinitions(std::vector<ItemDefinition> definitions) {
    m_definitions = std::move(definitions);
}

void ItemRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().items);
}

} // namespace legend::world
