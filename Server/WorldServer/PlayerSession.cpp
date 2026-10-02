#include "Server/WorldServer/PlayerSession.h"

namespace legend::world {

PlayerSession::PlayerSession(std::uint64_t connectionId, std::uint64_t accountId,
                             std::uint64_t characterId, const std::string& characterName,
                             std::uint16_t classId, std::uint16_t gender, std::uint32_t level,
                             std::uint16_t mapId, float positionX, float positionY,
                             std::uint16_t visualId)
    : m_connectionId(connectionId),
      m_accountId(accountId),
      m_characterId(characterId),
      m_characterName(characterName),
      m_classId(classId),
      m_gender(gender),
      m_level(level),
      m_mapId(mapId),
      m_visualId(visualId),
      m_positionX(positionX),
      m_positionY(positionY) {
    // 阶段17 指令二/十：加载角色时按持久化 level 初始化基础属性（Level 1 = 100/20/5）。
    m_maxHp = BaseMaxHpForLevel(level);
    m_currentHp = m_maxHp;
    m_attackPower = BaseAttackPowerForLevel(level);
    m_defense = BaseDefenseForLevel(level);
    // Derived 初始 = 等级基础值（状态变化时经 RecalculateDerivedStats 重算）。
    m_effectiveAttackPower = m_attackPower;
    m_effectiveDefense = m_defense;
}

} // namespace legend::world
