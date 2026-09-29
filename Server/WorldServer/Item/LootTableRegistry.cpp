#include "Server/WorldServer/Item/LootTableRegistry.h"

#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

const LootTableRegistry& LootTableRegistry::Instance() {
    static const LootTableRegistry registry;
    return registry;
}

LootTableRegistry& LootTableRegistry::Mutable() {
    return const_cast<LootTableRegistry&>(Instance());
}

const LootTableDefinition* LootTableRegistry::Find(std::uint32_t lootTableId) const {
    for (const auto& table : m_tables) {
        if (table.lootTableId == lootTableId) {
            return &table;
        }
    }
    return nullptr;
}

void LootTableRegistry::LoadFromDefinitions(std::vector<LootTableDefinition> tables) {
    Mutable().m_tables = std::move(tables);
}

void LootTableRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().lootTables);
}

} // namespace legend::world
