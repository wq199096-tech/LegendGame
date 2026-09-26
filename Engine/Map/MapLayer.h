#pragma once

#include <string>

namespace legend::map {

enum class MapLayerType {
    Tile,
    Object,
    Collision,
    Occlusion,
};

// 地图层基类：所有层共享名称 / 类型 / 可见性
class MapLayer {
public:
    MapLayer(std::string name, MapLayerType type);
    virtual ~MapLayer() = default;

    const std::string& GetName() const { return m_name; }
    MapLayerType GetType() const { return m_type; }

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    std::string m_name;
    MapLayerType m_type;
    bool m_visible = true;
};

} // namespace legend::map
