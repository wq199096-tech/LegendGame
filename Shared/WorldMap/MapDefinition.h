#pragma once

#include "Shared/WorldMap/MapTypes.h"

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令四/五：MapDefinition —— 服务器权威逻辑地图静态定义。
// 全部代码硬编码（MapRegistry），绝不入库（与 Definition 纪律一致）。
// ---------------------------------------------------------------------------
struct MapDefinition {
    std::uint16_t mapId = 1;
    std::string name;
    MapType type = MapType::Town;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 2000.0f;
    float maxY = 2000.0f;
    float spawnX = 300.0f;   // 出生/入口
    float spawnY = 300.0f;
    float respawnX = 300.0f; // 默认复活点（指令三十五：CurrentMap 复活用）
    float respawnY = 300.0f;
    // 阶段24：视觉地图引用（visual_maps.json 的 visualMapId；空 = Client 用 Fallback Grid）。
    // 纯视觉字段——服务器逻辑不使用，Client/Editor 用于地图渲染与资产绑定。
    std::string visualMapId;
    // 阶段25 指令四十二：Safe Zone（仅 Map1 新手村 metadata；radius<=0 = 无）。
    // 本阶段只作为地图元数据（未来 PvP 预留），不实现任何 PvP/怪物逻辑。
    float safeZoneX = 0.0f;
    float safeZoneY = 0.0f;
    float safeZoneRadius = 0.0f;

    // 边界内判断（含 NaN 防护：NaN 比较全 false -> 不在边界内）。
    bool InBounds(float x, float y) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    // 服务器权威 Clamp（指令八：客户端位置不能越界）。
    void Clamp(float& x, float& y) const {
        if (!(x >= minX)) {
            x = minX;
        } else if (x > maxX) {
            x = maxX;
        }
        if (!(y >= minY)) {
            y = minY;
        } else if (y > maxY) {
            y = maxY;
        }
    }
};

} // namespace legend::world
