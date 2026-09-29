#pragma once

#include "Shared/WorldMap/MapDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令七：MapRegistry —— 3 张服务器权威逻辑地图（代码硬编码，绝不入库）。
// 只读单例；启动 ValidateMaps 校验（mapId 唯一/边界合法/spawn·respawn 在边界内）。
// ---------------------------------------------------------------------------
class MapRegistry {
public:
    static const MapRegistry& Instance();

    MapRegistry();

    std::size_t Count() const { return m_maps.size(); }
    const MapDefinition* FindMap(std::uint16_t mapId) const;
    const std::vector<MapDefinition>& AllMaps() const { return m_maps; }
    const MapDefinition* TownMap() const { return FindMap(kTownMapId); }

    // 指令八：服务器权威边界 Clamp（地图不存在时回退 Town 边界）。
    void ClampToMap(std::uint16_t mapId, float& x, float& y) const;

    // 指令七：启动校验（唯一性/边界合法/spawn·respawn 在边界内）。
    bool ValidateMaps(std::string& error) const;

private:
    std::vector<MapDefinition> m_maps;
};

} // namespace legend::world
