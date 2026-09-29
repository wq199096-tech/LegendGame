#include "Server/WorldServer/Monster/MonsterDefinitionRegistry.h"

namespace legend::world {

const MonsterDefinitionRegistry& MonsterDefinitionRegistry::Instance() {
    static const MonsterDefinitionRegistry registry;
    return registry;
}

MonsterDefinitionRegistry::MonsterDefinitionRegistry() {
    // 阶段23 23.22：构造即填充出厂默认（直接填充——见 QuestRegistry 构造注释，
    // 经 Mutable()/Instance() 会在 MSVC magic-static 初始化中重入死锁）。
    m_definitions = {kTrainingSlimeDefinition};
}

MonsterDefinitionRegistry& MonsterDefinitionRegistry::Mutable() {
    return const_cast<MonsterDefinitionRegistry&>(Instance());
}

const MonsterDefinition* MonsterDefinitionRegistry::Find(std::uint32_t monsterTypeId) const {
    for (const auto& definition : m_definitions) {
        if (definition.monsterTypeId == monsterTypeId) {
            return &definition;
        }
    }
    return nullptr;
}

void MonsterDefinitionRegistry::LoadFromDefinitions(std::vector<MonsterDefinition> definitions) {
    Mutable().m_definitions = std::move(definitions);
}

void MonsterDefinitionRegistry::LoadDefaults() {
    LoadFromDefinitions({kTrainingSlimeDefinition});
}

} // namespace legend::world
