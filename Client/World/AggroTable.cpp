#include "Client/World/AggroTable.h"

#include <limits>

namespace legend::world {

void AggroTable::AddThreat(EntityId id, float amount) {
    for (Entry& entry : m_entries) {
        if (entry.id == id) {
            entry.threat += amount;
            return;
        }
    }
    m_entries.push_back({id, amount});
}

void AggroTable::Remove(EntityId id) {
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

EntityId AggroTable::GetHighestThreat() const {
    EntityId best = kInvalidEntityId;
    float bestThreat = -std::numeric_limits<float>::infinity();
    for (const Entry& entry : m_entries) {
        if (entry.threat > bestThreat) {
            bestThreat = entry.threat;
            best = entry.id;
        }
    }
    return best;
}

float AggroTable::GetThreat(EntityId id) const {
    for (const Entry& entry : m_entries) {
        if (entry.id == id) {
            return entry.threat;
        }
    }
    return 0.0f;
}

} // namespace legend::world
