#include "Engine/Map/MapLayer.h"

namespace legend::map {

MapLayer::MapLayer(std::string name, MapLayerType type)
    : m_name(std::move(name)), m_type(type) {}

} // namespace legend::map
