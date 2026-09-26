#include "Engine/Map/CollisionLayer.h"

namespace legend::map {

CollisionLayer::CollisionLayer() : MapLayer("CollisionLayer", MapLayerType::Collision) {}

void CollisionLayer::SetSize(int width, int height) {
    m_width = width;
    m_height = height;
    m_tiles.assign(static_cast<size_t>(width) * height, 0);
}

void CollisionLayer::SetData(std::vector<uint8_t>&& tiles, int width, int height) {
    m_tiles = std::move(tiles);
    m_width = width;
    m_height = height;
}

} // namespace legend::map
