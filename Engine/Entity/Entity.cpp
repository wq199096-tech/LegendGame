#include "Engine/Entity/Entity.h"

namespace legend::entity {

Entity::Entity(EntityId id, std::string name) : m_id(id), m_name(std::move(name)) {}

} // namespace legend::entity
