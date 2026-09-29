#pragma once

#include <cstdint>
#include <string>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段21 指令十八：RemotePortalEntity —— 服务器 Spawn 的传送门客户端镜像。
// 静态实体（不移动/不死亡）；目标地图名用于 F9 面板显示。
// ---------------------------------------------------------------------------
struct RemotePortalEntity {
    std::uint64_t portalEntityId = 0;
    std::uint32_t portalId = 0;
    std::string name;
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    float interactionRadius = 100.0f;
    std::uint16_t destinationMapId = 1;
    std::string destinationName;
    bool active = true;
};

} // namespace legend::client
