#pragma once

#include <memory>
#include <string>

#include "Engine/Map/Map.h"

namespace legend::map {

// 地图序列化：读取/写入 map.json（游戏与编辑器共用，版本 1）
class MapLoader {
public:
    // 加载失败返回 nullptr，并通过 Logger 输出明确错误，不抛异常不崩溃
    static std::shared_ptr<Map> Load(const std::string& filePath);

    // 保存失败返回 false 并记录错误日志
    static bool Save(const Map& map, const std::string& filePath);
};

} // namespace legend::map
