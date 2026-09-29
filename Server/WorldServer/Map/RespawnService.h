#pragma once

#include "Shared/WorldMap/RespawnProtocol.h"

#include <chrono>
#include <cstdint>

namespace legend::world {

class PlayerSession;

// ---------------------------------------------------------------------------
// 阶段21 指令三十三~四十/五十八：RespawnService —— 复活纯规则（不碰网络/DB）。
// PlanRespawn 产出复活计划（目的地/费用/错误码）；WorldServer 编排执行
//（扣 Gold -> 清状态 -> 复活 -> 统一 MapTransitionService / 同图重建 AOI）。
// ---------------------------------------------------------------------------
struct RespawnPlan {
    bool valid = false;
    RespawnResultCode code = RespawnResultCode::NotDead;
    std::uint16_t destMapId = 1;
    float destX = 0.0f;
    float destY = 0.0f;
    std::uint32_t goldCost = 0;
    bool crossMap = false; // 复活点在其它地图（如 Town）-> 走统一地图切换
};

class RespawnService {
public:
    // 指令三十四/三十五/三十六/三十七/三十八/五十八：
    // - mode 只允许 CurrentMap(1)/Town(2)，其它 InvalidMode；
    // - 必须处于死亡状态（NotDead）；死亡 >= kRespawnMinDelaySeconds（TooEarly）；
    // - CurrentMap = 当前地图 MapDefinition respawn 点，费用 10 Gold（不足拒绝）；
    // - Town = 固定 Map1 300,300，免费（任何地图死亡可用）。
    static RespawnPlan PlanRespawn(const PlayerSession& player, RespawnMode mode,
                                   std::chrono::steady_clock::time_point now);
};

} // namespace legend::world
