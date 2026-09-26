#include "Engine/Map/TileLayer.h"

namespace legend::map {

TileLayer::TileLayer() : MapLayer("TileLayer", MapLayerType::Tile) {}

void TileLayer::SetSize(int width, int height, uint16_t fillValue) {
    m_width = width;
    m_height = height;
    m_tiles.assign(static_cast<size_t>(width) * height, fillValue);
}

void TileLayer::SetData(std::vector<uint16_t>&& tiles, int width, int height) {
    m_tiles = std::move(tiles);
    m_width = width;
    m_height = height;
}

} // namespace legend::map
