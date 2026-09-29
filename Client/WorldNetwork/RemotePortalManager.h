#pragma once

#include "Client/WorldNetwork/RemotePortalEntity.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段21 指令十八/五十六：RemotePortalManager —— Portal 镜像容器。
// 与 RemoteNpcManager 同模式：Spawn 创建/覆盖，Despawn 立即移除（不留幽灵），
// 切图（MapChanged）整体 Clear。
// ---------------------------------------------------------------------------
class RemotePortalManager {
public:
    void Clear() { m_portals.clear(); }
    std::size_t Count() const { return m_portals.size(); }
    const RemotePortalEntity* Find(std::uint64_t portalEntityId) const {
        const auto it = m_portals.find(portalEntityId);
        return it != m_portals.end() ? &it->second : nullptr;
    }
    const std::unordered_map<std::uint64_t, RemotePortalEntity>& All() const { return m_portals; }

    // PortalSpawn(340)：不存在创建 / 已存在更新（字段完整覆盖）。
    void HandleSpawn(std::uint64_t portalEntityId, std::uint32_t portalId, const std::string& name,
                     std::uint16_t mapId, float x, float y, float interactionRadius,
                     std::uint16_t destinationMapId, const std::string& destinationName);
    // PortalDespawn(341)：立即移除。
    bool HandleDespawn(std::uint64_t portalEntityId);

private:
    std::unordered_map<std::uint64_t, RemotePortalEntity> m_portals;
};

} // namespace legend::client
