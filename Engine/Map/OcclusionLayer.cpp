#include "Engine/Map/OcclusionLayer.h"

#include <algorithm>

namespace legend::map {

OcclusionLayer::OcclusionLayer() : MapLayer("OcclusionLayer", MapLayerType::Occlusion) {}

bool OcclusionLayer::IsOccluder(uint32_t objectId) const {
    return std::find(m_occluderIds.begin(), m_occluderIds.end(), objectId) != m_occluderIds.end();
}

void OcclusionLayer::AddOccluder(uint32_t objectId) {
    if (!IsOccluder(objectId)) {
        m_occluderIds.push_back(objectId);
    }
}

void OcclusionLayer::RemoveOccluder(uint32_t objectId) {
    std::erase(m_occluderIds, objectId);
}

} // namespace legend::map
