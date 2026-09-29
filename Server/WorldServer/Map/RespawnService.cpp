#include "Server/WorldServer/Map/RespawnService.h"

#include "Server/WorldServer/Map/MapRegistry.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Shared/WorldMap/MapTypes.h"

namespace legend::world {

RespawnPlan RespawnService::PlanRespawn(const PlayerSession& player, RespawnMode mode,
                                        std::chrono::steady_clock::time_point now) {
    RespawnPlan plan;
    // 指令三十一：必须处于死亡状态。
    if (player.Alive()) {
        plan.code = RespawnResultCode::NotDead;
        return plan;
    }
    // 指令五十八：mode 只允许 1/2（Decode 后仍需语义校验）。
    if (mode != RespawnMode::CurrentMap && mode != RespawnMode::Town) {
        plan.code = RespawnResultCode::InvalidMode;
        return plan;
    }
    // 指令三十七：死亡后至少 3 秒才允许复活（服务器判断）。
    const std::chrono::duration<double> deadDuration = now - player.DeadSince();
    if (deadDuration.count() < kRespawnMinDelaySeconds) {
        plan.code = RespawnResultCode::TooEarly;
        return plan;
    }
    if (mode == RespawnMode::CurrentMap) {
        // 指令三十五/三十八：当前地图 respawn 点，费用 10 Gold。
        const MapDefinition* map = MapRegistry::Instance().FindMap(player.MapId());
        if (map == nullptr) {
            plan.code = RespawnResultCode::MapNotFound;
            return plan;
        }
        if (static_cast<std::uint64_t>(player.Gold()) < kRespawnCurrentMapGoldCost) {
            plan.code = RespawnResultCode::NotEnoughGold; // 指令三十八：不足拒绝，仍可 Town
            return plan;
        }
        plan.valid = true;
        plan.code = RespawnResultCode::Success;
        plan.destMapId = map->mapId;
        plan.destX = map->respawnX;
        plan.destY = map->respawnY;
        plan.goldCost = kRespawnCurrentMapGoldCost;
        plan.crossMap = false;
        return plan;
    }
    // 指令三十六：Town 复活 = 固定 Map1 300,300，免费（任何地图死亡可用）。
    const MapDefinition* town = MapRegistry::Instance().TownMap();
    if (town == nullptr) {
        plan.code = RespawnResultCode::MapNotFound;
        return plan;
    }
    plan.valid = true;
    plan.code = RespawnResultCode::Success;
    plan.destMapId = town->mapId;
    plan.destX = town->spawnX;
    plan.destY = town->spawnY;
    plan.goldCost = kRespawnTownGoldCost;
    plan.crossMap = player.MapId() != town->mapId;
    return plan;
}

} // namespace legend::world
