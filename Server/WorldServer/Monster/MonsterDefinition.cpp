#include "Server/WorldServer/Monster/MonsterDefinitionRegistry.h"

namespace legend::world {

// 阶段13 全局查找函数（保留签名——调用面零改动）；阶段23 起内部走注册表。
const MonsterDefinition* FindMonsterDefinition(std::uint32_t monsterTypeId) {
    return MonsterDefinitionRegistry::Instance().Find(monsterTypeId);
}

} // namespace legend::world
