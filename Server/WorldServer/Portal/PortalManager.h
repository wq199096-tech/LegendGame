#pragma once

#include "Shared/Portal/PortalDefinition.h"

#include <cstdint>
#include <map>
#include <vector>

namespace legend::world {

class PortalSpatialGrid;

// ---------------------------------------------------------------------------
// 阶段21 指令十四/十七：PortalManager —— Portal runtime 容器（io 线程独占）。
// Portal 静态不移动（同 NPC）：启动从 Registry 生成全部，EntityId 单调。
// ---------------------------------------------------------------------------
class PortalManager {
public:
    // 从 Registry 生成全部 Portal（WorldServer::Start 调用一次；返回生成数量）。
    std::size_t SpawnFromRegistry();

    const PortalDefinition* Find(std::uint64_t portalEntityId) const;
    std::size_t Count() const { return m_portals.size(); }

    // 供 SpatialGrid 构建使用（Portal 静态不移动——Add 一次即可）。
    void AddToGrid(PortalSpatialGrid& grid) const;

private:
    struct PortalEntity {
        std::uint64_t entityId = 0;
        const PortalDefinition* definition = nullptr;
    };

    std::map<std::uint64_t, PortalEntity> m_portals;
    std::uint64_t m_nextEntityId = 1; // runtime EntityId 单调（同 NPC 指令十）
};

} // namespace legend::world
