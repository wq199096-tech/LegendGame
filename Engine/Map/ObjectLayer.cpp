#include "Engine/Map/ObjectLayer.h"

#include <algorithm>

namespace legend::map {

ObjectLayer::ObjectLayer() : MapLayer("ObjectLayer", MapLayerType::Object) {}

MapObject& ObjectLayer::AddObject(MapObject object) {
    m_objects.push_back(std::move(object));
    return m_objects.back();
}

bool ObjectLayer::RemoveObject(uint32_t id) {
    const auto it = std::remove_if(m_objects.begin(), m_objects.end(),
                                   [id](const MapObject& object) { return object.id == id; });
    if (it == m_objects.end()) {
        return false;
    }
    m_objects.erase(it, m_objects.end());
    return true;
}

MapObject* ObjectLayer::FindObject(uint32_t id) {
    for (auto& object : m_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

const MapObject* ObjectLayer::FindObject(uint32_t id) const {
    for (const auto& object : m_objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

uint32_t ObjectLayer::GetMaxObjectId() const {
    uint32_t maxId = 0;
    for (const auto& object : m_objects) {
        maxId = std::max(maxId, object.id);
    }
    return maxId;
}

} // namespace legend::map
