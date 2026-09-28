#include "Server/WorldServer/Monster/MonsterDefinition.h"

namespace legend::world {

const MonsterDefinition* FindMonsterDefinition(std::uint32_t monsterTypeId) {
    if (monsterTypeId == kTrainingSlimeTypeId) {
        static const MonsterDefinition definition = kTrainingSlimeDefinition;
        return &definition;
    }
    return nullptr; // 指令五：阶段13 无其它类型（无 JSON 数据库/编辑器）
}

} // namespace legend::world
