#include "Server/WorldServer/PlayerSession.h"

namespace legend::world {

PlayerSession::PlayerSession(std::uint64_t connectionId, std::uint64_t accountId,
                             std::uint64_t characterId, const std::string& characterName,
                             std::uint16_t classId, std::uint16_t gender, std::uint32_t level,
                             std::uint16_t mapId, float positionX, float positionY)
    : m_connectionId(connectionId),
      m_accountId(accountId),
      m_characterId(characterId),
      m_characterName(characterName),
      m_classId(classId),
      m_gender(gender),
      m_level(level),
      m_mapId(mapId),
      m_positionX(positionX),
      m_positionY(positionY) {}

} // namespace legend::world
