#pragma once

#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段24：地图视觉定义（visual_maps.json）——纯视觉数据，服务器逻辑不使用。
// Client 按当前 mapId -> maps.json visualMapId -> 本定义渲染地图背景/层。
// 层固定四层（渲染顺序 Ground -> Decoration -> World Entities -> Object/Foreground）。
// ---------------------------------------------------------------------------
struct MapVisualPlacement {
    std::string assetId; // asset_manifest.json 的 assetId
    float x = 0.0f;      // 世界坐标（锚点 = 资源 pivot）
    float y = 0.0f;
};

struct MapVisualLayer {
    std::string name; // Ground / Decoration / Object / Foreground
    std::string assetId; // 层统一贴图（Ground 层用于平铺；其余层可空）
    std::vector<MapVisualPlacement> placements;
};

struct MapVisualDefinition {
    std::string visualMapId;
    std::string backgroundAsset; // Ground 平铺贴图 assetId（非空）
    float tileSize = 64.0f;      // 平铺单元世界尺寸
    std::vector<MapVisualLayer> layers;
};

} // namespace legend::world
