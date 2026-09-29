#pragma once

#include "Shared/Portal/PortalDefinition.h"
#include "Shared/Portal/PortalTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

class MapRegistry;

// ---------------------------------------------------------------------------
// 阶段21 指令十四/十六/七十：PortalRegistry —— 4 个固定传送门（代码硬编码）。
// 只读单例；启动 ValidatePortals 校验（portalId 唯一/源与目标地图存在/
// 源与目标坐标在对应地图边界内）。
// ---------------------------------------------------------------------------
class PortalRegistry {
public:
    static const PortalRegistry& Instance();

    PortalRegistry();

    std::size_t Count() const { return m_portals.size(); }
    const PortalDefinition* FindPortal(std::uint32_t portalId) const;
    const std::vector<PortalDefinition>& AllPortals() const { return m_portals; }

    bool ValidatePortals(const MapRegistry& maps, std::string& error) const;

private:
    std::vector<PortalDefinition> m_portals;
};

} // namespace legend::world
