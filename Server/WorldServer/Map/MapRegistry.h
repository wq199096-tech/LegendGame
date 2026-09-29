#pragma once

#include "Shared/WorldMap/MapDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令七 → 阶段22 22.11 改造：MapRegistry —— 服务器权威逻辑地图注册表。
// 生产从 Data/World/maps.json 加载（WorldServer::Initialize 统一注入）；
// 目录缺失时用 MakeDefaultWorldData 的出厂配置（22.18 迁移源，单一事实）。
// 只读访问走 Instance()（const&，调用面零改动）；数据注入走静态 Load*。
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

    // 数据注入（WorldServer::Initialize / 测试 fixture）。
    static void LoadFromDefinitions(std::vector<MapDefinition> maps);
    static void LoadDefaults();

private:
    static MapRegistry& Mutable();

    std::vector<MapDefinition> m_maps;
};

} // namespace legend::world
