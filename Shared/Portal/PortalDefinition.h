#pragma once

#include "Shared/Portal/PortalTypes.h"

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令十五：PortalDefinition —— 传送门静态定义（代码硬编码，绝不入库）。
// Client 不能上传目标 map/坐标/费用（指令二十）——全部由本定义服务器权威决定。
// ---------------------------------------------------------------------------
struct PortalDefinition {
    std::uint32_t portalId = 0;
    std::uint16_t sourceMapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    float interactionRadius = 100.0f; // 指令十九：进入半径后按 F 触发（不自动传送）
    std::uint16_t destinationMapId = 1;
    float destinationX = 0.0f;
    float destinationY = 0.0f;
    std::uint32_t minLevel = 1;
    std::uint32_t goldCost = 0;
    bool enabled = true;
};

} // namespace legend::world
