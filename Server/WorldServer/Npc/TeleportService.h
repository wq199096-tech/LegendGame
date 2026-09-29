#pragma once

#include "Shared/Teleport/TeleportDefinition.h"
#include "Shared/Teleport/TeleportTypes.h"

#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令五十九/六十 → 阶段23 23.22：TeleportRegistry —— 传送点注册表。
// 生产从 Data/Game/teleports.json 加载（WorldServer::Start 注入）；
// 只读单例；NpcRegistry 启动校验引用。
// ---------------------------------------------------------------------------
class TeleportRegistry {
public:
    static const TeleportRegistry& Instance();

    TeleportRegistry();

    const TeleportDefinition* FindTeleport(std::uint32_t teleportId) const;

    // 数据注入（WorldServer::Start；阶段23 23.22）。
    static void LoadFromDefinitions(std::vector<TeleportDefinition> teleports);
    static void LoadDefaults();

private:
    static TeleportRegistry& Mutable();

    std::vector<TeleportDefinition> m_teleports;
};

} // namespace legend::world
