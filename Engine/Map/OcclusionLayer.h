#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Map/MapLayer.h"

namespace legend::map {

// 遮挡层：记录参与前景遮挡（Y-Sort 与玩家前后关系）的物件 id。
// 未来扩展：建筑屋顶淡化、多层遮挡规则。
class OcclusionLayer : public MapLayer {
public:
    OcclusionLayer();

    bool IsOccluder(uint32_t objectId) const;
    void AddOccluder(uint32_t objectId);
    void SetOccluderIds(const std::vector<uint32_t>& ids) { m_occluderIds = ids; }

    const std::vector<uint32_t>& GetOccluderIds() const { return m_occluderIds; }

private:
    std::vector<uint32_t> m_occluderIds;
};

} // namespace legend::map
