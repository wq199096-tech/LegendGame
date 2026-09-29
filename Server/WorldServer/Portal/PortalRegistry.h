#pragma once

#include "Shared/Portal/PortalDefinition.h"
#include "Shared/Portal/PortalTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

class MapRegistry;

// ---------------------------------------------------------------------------
// 阶段21 指令十四/十六/七十 → 阶段22 22.11 改造：PortalRegistry —— 传送门注册表。
// 生产从 Data/World/portals.json 加载（WorldServer::Initialize 统一注入）；
// 目录缺失时用 MakeDefaultWorldData 的出厂配置（22.18 迁移源）。
// 只读访问走 Instance()；数据注入走静态 Load*。
// ---------------------------------------------------------------------------
class PortalRegistry {
public:
    static const PortalRegistry& Instance();

    PortalRegistry();

    std::size_t Count() const { return m_portals.size(); }
    const PortalDefinition* FindPortal(std::uint32_t portalId) const;
    const std::vector<PortalDefinition>& AllPortals() const { return m_portals; }

    bool ValidatePortals(const MapRegistry& maps, std::string& error) const;

    // 数据注入（WorldServer::Initialize / 测试 fixture）。
    static void LoadFromDefinitions(std::vector<PortalDefinition> portals);
    static void LoadDefaults();

private:
    static PortalRegistry& Mutable();

    std::vector<PortalDefinition> m_portals;
};

} // namespace legend::world
