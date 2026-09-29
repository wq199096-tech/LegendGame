#pragma once

#include "Shared/Teleport/TeleportDefinition.h"
#include "Shared/Teleport/TeleportTypes.h"

#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令五十九/六十：TeleportRegistry —— 传送点 7001/7002 硬编码注册表。
// 只读单例；NpcRegistry 启动校验引用。
// ---------------------------------------------------------------------------
class TeleportRegistry {
public:
    static const TeleportRegistry& Instance();

    TeleportRegistry();

    const TeleportDefinition* FindTeleport(std::uint32_t teleportId) const;

private:
    std::vector<TeleportDefinition> m_teleports;
};

} // namespace legend::world
