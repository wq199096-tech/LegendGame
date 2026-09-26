#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Engine/Map/MapLayer.h"

namespace legend::map {

// 地图静态物件（position 为中心点）
struct MapObject {
    uint32_t id = 0;
    std::string name;
    std::string textureId;
    float x = 0.0f;
    float y = 0.0f;
    float width = 64.0f;
    float height = 64.0f;
    float rotationDegrees = 0.0f;
    int renderOrder = 0;
    int sortLayer = 0; // Y-Sort 层：同层内严格按 bottomY 排序，renderOrder 只作平局判定
    bool blocking = false;
    bool occluder = false;

    // Y-Sort 键：物件底部世界 Y
    float GetBottomY() const { return y + height * 0.5f; }

    bool ContainsPoint(float worldX, float worldY) const {
        return worldX >= x - width * 0.5f && worldX <= x + width * 0.5f &&
               worldY >= y - height * 0.5f && worldY <= y + height * 0.5f;
    }
};

// 物件层：树 / 石头 / 建筑 / 装饰等静态对象
class ObjectLayer : public MapLayer {
public:
    ObjectLayer();

    MapObject& AddObject(MapObject object);
    // 按 id 删除；返回是否删除成功
    bool RemoveObject(uint32_t id);
    MapObject* FindObject(uint32_t id);
    const MapObject* FindObject(uint32_t id) const;
    // 获取当前最大 id（新物件 id 自增用）
    uint32_t GetMaxObjectId() const;

    std::vector<MapObject>& Objects() { return m_objects; }
    const std::vector<MapObject>& Objects() const { return m_objects; }
    void Clear() { m_objects.clear(); }

private:
    std::vector<MapObject> m_objects;
};

} // namespace legend::map
