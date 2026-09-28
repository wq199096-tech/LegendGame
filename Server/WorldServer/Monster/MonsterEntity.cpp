#include "Server/WorldServer/Monster/MonsterEntity.h"

namespace legend::world {

MonsterEntity::MonsterEntity(std::uint64_t entityId, std::uint32_t monsterTypeId,
                             std::uint16_t mapId, float spawnX, float spawnY, float moveSpeed)
    : m_entityId(entityId),
      m_monsterTypeId(monsterTypeId),
      m_mapId(mapId),
      m_positionX(spawnX),
      m_positionY(spawnY),
      m_spawnX(spawnX),
      m_spawnY(spawnY),
      m_moveSpeed(moveSpeed) {}

} // namespace legend::world
