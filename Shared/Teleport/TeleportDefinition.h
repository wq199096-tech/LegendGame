#pragma once

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令五十八/六十一：Teleport 静态定义（只能经 NPC Dialogue Option 触发
// ——指令六十二）。代码硬编码，不入数据库（指令一百零八）。
// ---------------------------------------------------------------------------
struct TeleportDefinition {
    std::uint32_t teleportId = 0;
    std::string name;
    std::uint16_t destinationMapId = 1;
    float destinationX = 0.0f;
    float destinationY = 0.0f;
    std::uint32_t goldCost = 0;
    std::uint32_t minLevel = 1;
    bool enabled = true; // 阶段23 23.10：数据驱动开关
};

} // namespace legend::world
